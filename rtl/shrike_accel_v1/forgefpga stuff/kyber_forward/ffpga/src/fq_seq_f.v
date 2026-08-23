// fq_seq_f
// forward ntt sequencer only
// cooley tukey butterflies, incremental addressing.
// contains no inverse / scale / basemul logic (nothing to prune) to save space 
// (offloaded to inverse and basemul hence the three bitstream approach)
// port compatible with fq_seq_i / fq_seq_b (b_* ports present but unused)

// this is the most jank piece of shit code i may have written in my life, but again its my fault for going with
// a 1.2k LUT fpga instead of an ice40up5k and then trying to cram a kyber accelerator in here (hence why we went 177% over capacity :LO:)

// why the split approach? why not use a shared arithmetic core, like before?
// good question. been there, done that. thing is, the shared arithmetic core by itself was nice for saving space, but
// the pipelining and muxing and whatnot to attach shared core to every other unit (forward, backward, basemul) 
// was taking up so much space on the freaking board that it would be wiser to just split off 
// the three core processes and give them their own arithmetic core.

module fq_seq_f (
    input             clk,
    input             rst,
    input             start,
    input      [1:0]  op,           // ignored (always forward)
    output reg        done,
    output reg [7:0]  a_raddr, output reg a_ren,  input [15:0] a_rdata,
    output reg [7:0]  a_waddr, output reg [15:0] a_wdata, output reg a_wen,
    output reg [7:0]  w_addr,  output reg w_ren,  input [15:0] w_rdata,
    output reg [7:0]  b_raddr, output reg b_ren,  input [15:0] b_rdata,
    output reg        fq_go, output reg [11:0] fq_a, output reg [11:0] fq_b,
    input             fq_done, input [11:0] fq_res
);
    // ---- ALU: add/sub mod q, compare-reduce, no dedicated adder core ----
    reg  [11:0] alu_x, alu_y; reg alu_sub;
    wire [12:0] yop  = alu_sub ? (13'd3329 - {1'b0,alu_y}) : {1'b0,alu_y};
    wire [13:0] rsum = {2'b0,alu_x} + {1'b0,yop};
    wire [11:0] alu_res = (rsum >= 14'd3329) ? (rsum[11:0] - 12'd3329) : rsum[11:0];

    localparam T_IDLE=4'd0, BF_RB=4'd1, BF_CB=4'd2, BF_CA=4'd3, BF_MUL=4'd4,
               BF_WMU=4'd5, BF_WA=4'd6, BF_WB=4'd7, BF_NX=4'd8, T_DONE=4'd9;
    reg [3:0] state;

    reg [2:0] layer;  reg [6:0] bidx;
    reg [7:0] aa, bb, widx, wbase, len;
    reg [11:0] a_reg, b_reg, tw_reg, t_reg;

    always @(posedge clk) begin
        if (rst) begin state<=T_IDLE; done<=1'b0; end
        else begin
            done <= 1'b0;
            case (state)
            T_IDLE: if (start) begin
                layer<=3'd0; bidx<=7'd0;
                len<=8'd128; aa<=8'd0; bb<=8'd128; widx<=8'd1; wbase<=8'd1;
                state<=BF_RB;
            end
            BF_RB:  state <= BF_CB;
            BF_CB:  begin b_reg<=a_rdata[11:0]; tw_reg<=w_rdata[11:0]; state<=BF_CA; end
            BF_CA:  begin a_reg<=a_rdata[11:0]; state<=BF_MUL; end
            BF_MUL: state <= BF_WMU;
            BF_WMU: if (fq_done) begin t_reg<=fq_res; state<=BF_WA; end
            BF_WA:  state <= BF_WB;
            BF_WB:  state <= BF_NX;
            BF_NX:  begin
                if (bidx == 7'd127) begin
                    bidx <= 7'd0;
                    if (layer == 3'd6) state <= T_DONE;
                    else begin
                        layer <= layer + 3'd1;
                        len<=len>>1; aa<=8'd0; bb<=(len>>1);
                        widx<=(wbase<<1); wbase<=(wbase<<1);
                        state <= BF_RB;
                    end
                end else begin
                    bidx <= bidx + 7'd1;
                    if ((aa & (len-8'd1)) == (len-8'd1)) begin
                        aa   <= aa + len + 8'd1;
                        bb   <= aa + len + 8'd1 + len;
                        widx <= widx + 8'd1;
                    end else begin
                        aa <= aa + 8'd1;
                        bb <= bb + 8'd1;
                    end
                    state <= BF_RB;
                end
            end
            T_DONE: begin done<=1'b1; state<=T_IDLE; end
            default: state <= T_IDLE;
            endcase
        end
    end

    // ALU operands: WA -> a+t ; WB -> a-t
    always @(*) begin
        alu_x = a_reg; alu_y = t_reg;
        alu_sub = (state == BF_WB);
    end

    // memory + arithmetic drive
    always @(*) begin
        a_raddr=8'd0; a_ren=1'b0; a_waddr=8'd0; a_wdata=16'd0; a_wen=1'b0;
        w_addr=8'd0;  w_ren=1'b0; b_raddr=8'd0; b_ren=1'b0;
        fq_go=1'b0; fq_a=12'd0; fq_b=12'd0;
        case (state)
            BF_RB:  begin a_raddr=bb; a_ren=1'b1; w_addr=widx; w_ren=1'b1; end
            BF_CB:  begin a_raddr=aa; a_ren=1'b1; end
            BF_MUL: begin fq_go=1'b1; fq_a=b_reg; fq_b=tw_reg; end
            BF_WA:  begin a_waddr=aa; a_wen=1'b1; a_wdata={4'd0,alu_res}; end
            BF_WB:  begin a_waddr=bb; a_wen=1'b1; a_wdata={4'd0,alu_res}; end
            default: ;
        endcase
    end
endmodule
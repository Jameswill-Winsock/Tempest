// fq_seq_b 
// basemul sequencer only.  A (*) B -> A pairwise in ntt domain:
//   r0 = fqmul(fqmul(a1,b1), zeta) + fqmul(a0,b0)
//   r1 = fqmul(a0,b1) + fqmul(a1,b0)
// zeta = zetas[64 + m/2], negated (q - z) for odd pairs.
// alu is add only (no subtract path exists)
// no butterfly / scale logic

// this is the most jank piece of shit code i may have written in my life, but again its my fault for going with
// a 1.2k LUT fpga instead of an ice40up5k and then trying to cram a kyber accelerator in here (hence why we went 177% over capacity :LO:)

// why the split approach? why not use a shared arithmetic core, like before?
// good question. been there, done that. thing is, the shared arithmetic core by itself was nice for saving space, but
// the pipelining and muxing and whatnot to attach shared core to every other unit (forward, backward, basemul) 
// was taking up so much space on the freaking board that it would be wiser to just split off 
// the three core processes and give them their own arithmetic core.

module fq_seq_b (
    input             clk,
    input             rst,
    input             start,
    input      [1:0]  op,           // ignored (always basemul)
    output reg        done,
    output reg [7:0]  a_raddr, output reg a_ren,  input [15:0] a_rdata,
    output reg [7:0]  a_waddr, output reg [15:0] a_wdata, output reg a_wen,
    output reg [7:0]  w_addr,  output reg w_ren,  input [15:0] w_rdata,
    output reg [7:0]  b_raddr, output reg b_ren,  input [15:0] b_rdata,
    output reg        fq_go, output reg [11:0] fq_a, output reg [11:0] fq_b,
    input             fq_done, input [11:0] fq_res
);
    localparam [11:0] Q = 12'd3329;

    // ADD-only mod-q reduce
    reg  [11:0] alu_x, alu_y;
    wire [12:0] rsum = {1'b0,alu_x} + {1'b0,alu_y};
    wire [11:0] alu_res = (rsum >= 13'd3329) ? (rsum[11:0] - 12'd3329) : rsum[11:0];

    localparam T_IDLE=3'd0, BM_R0=3'd1, BM_R1=3'd2, BM_CAP=3'd3,
               BM_GO=3'd4, BM_WT=3'd5, BM_WR=3'd6, T_DONE=3'd7;
    reg [2:0] state;

    reg [7:0]  m;
    reg [2:0]  step;
    reg        wr_hi;
    reg [11:0] a0,a1,b0,b1,zz,t1,tA,r0,r1;

    always @(posedge clk) begin
        if (rst) begin state<=T_IDLE; done<=1'b0; end
        else begin
            done <= 1'b0;
            case (state)
            T_IDLE: if (start) begin m<=8'd0; state<=BM_R0; end
            BM_R0:  state <= BM_R1;
            BM_R1:  begin a0<=a_rdata[11:0]; b0<=b_rdata[11:0]; zz<=w_rdata[11:0]; state<=BM_CAP; end
            BM_CAP: begin a1<=a_rdata[11:0]; b1<=b_rdata[11:0];
                          zz <= m[0] ? (Q - zz) : zz;
                          step<=3'd0; state<=BM_GO; end
            BM_GO:  state <= BM_WT;
            BM_WT:  if (fq_done) begin
                        case (step)
                            3'd0: begin t1<=fq_res;  step<=3'd1; state<=BM_GO; end
                            3'd1: begin tA<=fq_res;  step<=3'd2; state<=BM_GO; end
                            3'd2: begin r0<=alu_res; step<=3'd3; state<=BM_GO; end
                            3'd3: begin tA<=fq_res;  step<=3'd4; state<=BM_GO; end
                            default: begin r1<=alu_res; wr_hi<=1'b0; state<=BM_WR; end
                        endcase
                    end
            BM_WR:  if (wr_hi) begin
                        if (m==8'd127) state<=T_DONE;
                        else begin m<=m+8'd1; state<=BM_R0; end
                    end else wr_hi<=1'b1;
            T_DONE: begin done<=1'b1; state<=T_IDLE; end
            default: state <= T_IDLE;
            endcase
        end
    end

    always @(*) begin alu_x=tA; alu_y=fq_res; end

    wire [7:0] idx0 = {m[6:0],1'b0};
    wire [7:0] idx1 = {m[6:0],1'b0} | 8'd1;
    wire [7:0] zidx = 8'd64 + {1'b0,m[7:1]};

    always @(*) begin
        a_raddr=8'd0; a_ren=1'b0; a_waddr=8'd0; a_wdata=16'd0; a_wen=1'b0;
        w_addr=8'd0;  w_ren=1'b0; b_raddr=8'd0; b_ren=1'b0;
        fq_go=1'b0; fq_a=12'd0; fq_b=12'd0;
        case (state)
            BM_R0:  begin a_raddr=idx0; a_ren=1'b1; b_raddr=idx0; b_ren=1'b1;
                          w_addr=zidx; w_ren=1'b1; end
            BM_R1:  begin a_raddr=idx1; a_ren=1'b1; b_raddr=idx1; b_ren=1'b1; end
            BM_GO:  begin fq_go=1'b1;
                        case (step)
                            3'd0: begin fq_a=a1; fq_b=b1; end
                            3'd1: begin fq_a=t1; fq_b=zz; end
                            3'd2: begin fq_a=a0; fq_b=b0; end
                            3'd3: begin fq_a=a0; fq_b=b1; end
                            default: begin fq_a=a1; fq_b=b0; end
                        endcase end
            BM_WR:  begin a_wen=1'b1;
                        if (wr_hi) begin a_waddr=idx1; a_wdata={4'd0,r1}; end
                        else       begin a_waddr=idx0; a_wdata={4'd0,r0}; end end
            default: ;
        endcase
    end
endmodule

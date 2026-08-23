// ============================================================================
// fqmul_unit - serial 12-cycle a*b + combinational montgomery reduce.
//   go -> exactly 12 clocks of right-shift-accumulate -> done, res valid.
// One 13-bit adder for the multiply. REDC = constant shift-adds only, because
// q' = 3327 = 2^11+2^10+2^8-1 and q = 3329 = 2^11+2^10+2^8+1 (shifts are wires).
// no handshake protocol: fixed latency, caller watches done.
// why? because that took up a good chunk of LUTs; we precalculate and then based on that instead of repeatedly polling
// ("are we there yet" "are we there yet" "are we there yet") rp2040 waits fixed cycles.
// ============================================================================
module fqmul_unit (
    input             clk,
    input             rst,
    input             go,
    input      [11:0] a,
    input      [11:0] b,
    output reg        done,
    output     [11:0] res
);
    localparam [12:0] Q = 13'd3329;

    reg  [23:0] p;        // product accumulator (right-shift method)
    reg  [11:0] ar, br;
    reg  [3:0]  cnt;
    reg         busy;

    wire [12:0] padd = {1'b0, p[23:12]} + {1'b0, (ar[0] ? br : 12'd0)};

    always @(posedge clk) begin
        if (rst) begin busy<=1'b0; done<=1'b0; end
        else begin
            done <= 1'b0;
            if (go && !busy) begin
                p <= 24'd0; ar <= a; br <= b; cnt <= 4'd0; busy <= 1'b1;
            end else if (busy) begin
                p   <= {padd, p[11:1]};      // add-at-top then shift right
                ar  <= {1'b0, ar[11:1]};
                if (cnt == 4'd11) begin busy<=1'b0; done<=1'b1; end
                cnt <= cnt + 4'd1;
            end
        end
    end

    // ---- combinational REDC on the finished product ----
    wire [23:0] t  = p;
    wire [15:0] tl = t[15:0];
    wire [15:0] m  = (tl << 11) + (tl << 10) + (tl << 8) - tl;
    wire [27:0] mq = ({12'd0,m} << 11) + ({12'd0,m} << 10) + ({12'd0,m} << 8) + {12'd0,m};
    wire [27:0] s  = {4'd0, t} + mq;
    wire [12:0] u  = s[27:16];
    assign res = (u >= Q) ? (u - Q) : u[11:0];
endmodule

//  ntt_top 
//  kyber forward/inverse ntt + basemul accelerator
//  rp2040  <-- spi -->  fpga  <--->  slg47910 bram hard macros

//  bram map (RATIO=00, 512x8 each, all enables active low, registered read):
//    BRAM0/1 = operand A / coefficient buffer  (lo / hi byte)
//    BRAM2/3 = twiddle ROM  zetas[0..127]       (lo / hi byte)
//    BRAM4/5 = operand B  (second poly, basemul)(lo / hi byte)
//
//  instruction set (1 opcode byte, then payload; each byte = one spi exchange):
//    0x10 LOAD_A    : 512 bytes -> A      (lo,hi,...)
//    0x11 LOAD_B    : 512 bytes -> B      (lo,hi,...)
//    0x20 LOAD_ZETA : 256 bytes -> zetas  (lo,hi,...)
//    0x30 START_FWD : forward ntt on A
//    0x31 START_INV : inverse ntt on A
//    0x32 START_BMUL: basemul  A (*) B -> A
//    0x40 STATUS    : next miso byte = {7'b0, done}
//    0x50 READ_A    : 512 bytes <- A  (result, lo,hi,...)
// ============================================================================
(* top *) module ntt_top (
    (* iopad_external_pin, clkbuf_inhibit *) input  clk,
    (* iopad_external_pin *)                 output clk_en,

    // ---- SPI (RP2040 is controller) ----
    (* iopad_external_pin *) input  spi_ss_n,
    (* iopad_external_pin *) input  spi_sck,
    (* iopad_external_pin *) input  spi_mosi,
    (* iopad_external_pin *) input  rst_n,
    (* iopad_external_pin *) output spi_miso,
    (* iopad_external_pin *) output spi_miso_en,

    // ---- BRAM0 : coeff/A low byte ----
    (* iopad_external_pin *) output [1:0] BRAM0_RATIO,
    (* iopad_external_pin *) output [7:0] BRAM0_DATA_IN,
    (* iopad_external_pin *) output       BRAM0_WEN,
    (* iopad_external_pin *) output       BRAM0_WCLKEN,
    (* iopad_external_pin *) output [8:0] BRAM0_WRITE_ADDR,
    (* iopad_external_pin *) input  [7:0] BRAM0_DATA_OUT,
    (* iopad_external_pin *) output       BRAM0_REN,
    (* iopad_external_pin *) output       BRAM0_RCLKEN,
    (* iopad_external_pin *) output [8:0] BRAM0_READ_ADDR,
    // ---- BRAM1 : coeff/A high byte ----
    (* iopad_external_pin *) output [1:0] BRAM1_RATIO,
    (* iopad_external_pin *) output [7:0] BRAM1_DATA_IN,
    (* iopad_external_pin *) output       BRAM1_WEN,
    (* iopad_external_pin *) output       BRAM1_WCLKEN,
    (* iopad_external_pin *) output [8:0] BRAM1_WRITE_ADDR,
    (* iopad_external_pin *) input  [7:0] BRAM1_DATA_OUT,
    (* iopad_external_pin *) output       BRAM1_REN,
    (* iopad_external_pin *) output       BRAM1_RCLKEN,
    (* iopad_external_pin *) output [8:0] BRAM1_READ_ADDR,
    // ---- BRAM2 : twiddle low byte ----
    (* iopad_external_pin *) output [1:0] BRAM2_RATIO,
    (* iopad_external_pin *) output [7:0] BRAM2_DATA_IN,
    (* iopad_external_pin *) output       BRAM2_WEN,
    (* iopad_external_pin *) output       BRAM2_WCLKEN,
    (* iopad_external_pin *) output [8:0] BRAM2_WRITE_ADDR,
    (* iopad_external_pin *) input  [7:0] BRAM2_DATA_OUT,
    (* iopad_external_pin *) output       BRAM2_REN,
    (* iopad_external_pin *) output       BRAM2_RCLKEN,
    (* iopad_external_pin *) output [8:0] BRAM2_READ_ADDR,
    // ---- BRAM3 : twiddle high byte ----
    (* iopad_external_pin *) output [1:0] BRAM3_RATIO,
    (* iopad_external_pin *) output [7:0] BRAM3_DATA_IN,
    (* iopad_external_pin *) output       BRAM3_WEN,
    (* iopad_external_pin *) output       BRAM3_WCLKEN,
    (* iopad_external_pin *) output [8:0] BRAM3_WRITE_ADDR,
    (* iopad_external_pin *) input  [7:0] BRAM3_DATA_OUT,
    (* iopad_external_pin *) output       BRAM3_REN,
    (* iopad_external_pin *) output       BRAM3_RCLKEN,
    (* iopad_external_pin *) output [8:0] BRAM3_READ_ADDR
);


    assign clk_en = 1'b1;
    wire rst = ~rst_n;

    assign BRAM0_RATIO = 2'b00;
    assign BRAM1_RATIO = 2'b00;
    assign BRAM2_RATIO = 2'b00;
    assign BRAM3_RATIO = 2'b00;

    // combined 16-bit read data
    wire [15:0] a_rdata  = {BRAM1_DATA_OUT, BRAM0_DATA_OUT};   // coeff / operand A
    wire [15:0] tw_rdata = {BRAM3_DATA_OUT, BRAM2_DATA_OUT};   // twiddle

    // ------------------------------------------------------------------------
    //  SPI target (canonical Shrike module, mode 0, MSB, 8-bit)
    // ------------------------------------------------------------------------
    wire [7:0] rx_data;
    wire       rx_valid;
    reg  [7:0] tx_byte;
    wire       tx_hold;

    spi_target #(.CPOL(0), .CPHA(0), .WIDTH(8), .LSB(0)) u_spi (
        .i_clk          (clk),
        .i_rst_n        (rst_n),
        .i_enable       (1'b1),
        .i_ss_n         (spi_ss_n),
        .i_sck          (spi_sck),
        .i_mosi         (spi_mosi),
        .o_miso         (spi_miso),
        .o_miso_oe      (spi_miso_en),
        .o_rx_data      (rx_data),
        .o_rx_data_valid(rx_valid),
        .i_tx_data      (tx_byte),
        .o_tx_data_hold ()
    );

    reg  rx_valid_d;
    wire rx_pulse = rx_valid & ~rx_valid_d;
    always @(posedge clk or negedge rst_n)
        if (!rst_n) rx_valid_d <= 1'b0;
        else        rx_valid_d <= rx_valid;

    // ------------------------------------------------------------------------
    //  UNIFIED engine: one fq_seq (fwd/inv/scale/basemul) + one fq_core.
    //  Replaces ntt_master + ntt_butterfly + basemul_ctrl + mod_mult + mont_redc.
    // ------------------------------------------------------------------------
    reg        eng_start; reg [1:0] eng_op;
    wire       eng_done;
    wire [7:0] e_araddr, e_awaddr, e_waddr, e_braddr;
    wire       e_aren, e_awen, e_wren, e_bren;
    wire [15:0] e_awdata;
    wire        e_go, e_dn; wire [11:0] e_fa, e_fb, e_fr;

    fq_seq_i u_seq (
        .clk(clk), .rst(rst), .start(eng_start), .op(eng_op), .done(eng_done),
        .a_raddr(e_araddr), .a_ren(e_aren), .a_rdata(a_rdata),
        .a_waddr(e_awaddr), .a_wdata(e_awdata), .a_wen(e_awen),
        .w_addr(e_waddr), .w_ren(e_wren), .w_rdata(tw_rdata),
        .b_raddr(e_braddr), .b_ren(e_bren), .b_rdata(b_rdata),
        .fq_go(e_go), .fq_a(e_fa), .fq_b(e_fb), .fq_done(e_dn), .fq_res(e_fr)
    );
    fqmul_unit u_fq (
        .clk(clk), .rst(rst), .go(e_go), .a(e_fa), .b(e_fb), .done(e_dn), .res(e_fr)
    );

    wire [15:0] b_rdata = 16'd0;
    wire eng_run = run | bmrun;   // engine owns the BRAMs while active

    // ------------------------------------------------------------------------
    //  Command decoder
    // ------------------------------------------------------------------------
    localparam H_IDLE = 2'd0,
               H_WRA  = 2'd1,   // load operand A
               H_WRB  = 2'd2,   // load operand B
               H_WRZ  = 2'd3;   // load zetas
    // read-back uses a dedicated flag (only 2 bits for hmode)
    reg  [1:0] hmode;
    reg        rdc;             // read-back A active
    reg  [9:0] bptr;
    reg        run;             // NTT active
    reg        bmrun;           // basemul active
    reg        dlatch;          // last op done

    wire [7:0] word_addr = bptr[9:1];
    wire       hi        = bptr[0];
    // read-back prefetch: address the word one step ahead so registered BRAM
    // data is valid when the vendor SPI target latches tx_byte.
    wire [9:0] rd_ptr    = rdc ? (bptr + 10'd2) : bptr;  // +2 prefetch: host discards first 2 read words
    wire [7:0] rd_waddr  = rd_ptr[9:1];

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            hmode     <= H_IDLE;
            rdc       <= 1'b0;
            bptr      <= 10'd0;
            run       <= 1'b0;
            bmrun     <= 1'b0;
            dlatch    <= 1'b0;
            eng_start <= 1'b0;
            eng_op    <= 2'd0;
        end else begin
            eng_start <= 1'b0;

            if (eng_done) begin dlatch <= 1'b1; run <= 1'b0; bmrun <= 1'b0; end

            if (rx_pulse) begin
                if (hmode == H_IDLE && !rdc) begin
                    case (rx_data)
                        8'h10: begin hmode <= H_WRA; bptr <= 10'd0; end
                                                8'h20: begin hmode <= H_WRZ; bptr <= 10'd0; end
                                                8'h31: begin eng_start <= 1'b1; eng_op <= 2'd1; run   <= 1'b1; dlatch <= 1'b0; end
                                                8'h50: begin rdc <= 1'b1; bptr <= 10'd0; end
                        default: ; // 0x40 STATUS: tx_byte already reflects dlatch
                    endcase
                end else if (rdc) begin
                    if (bptr == 10'd511) rdc <= 1'b0;
                    bptr <= bptr + 10'd1;
                end else begin
                    // streaming a load (A / B: 512 bytes, Z: 256 bytes)
                    if ((hmode == H_WRZ && bptr == 10'd255) ||
                        (hmode != H_WRZ && bptr == 10'd511)) hmode <= H_IDLE;
                    bptr <= bptr + 10'd1;
                end
            end
        end
    end

    // MISO byte: combinational. The vendor SPI target latches i_tx_data into its
    // shift register at o_tx_data_hold (start of each outgoing byte), so tx_byte
    // just needs to be valid then. rd_byte follows the current read pointer.
    wire [7:0] rd_byte = hi ? BRAM1_DATA_OUT : BRAM0_DATA_OUT;
    always @(*) tx_byte = rdc ? rd_byte : {7'b0, dlatch};

    // ------------------------------------------------------------------------
    //  BRAM arbitration :  run -> butterfly ,  bmrun -> basemul ,  else -> host
    // ------------------------------------------------------------------------
    wire host_wr_a = (hmode == H_WRA) & rx_pulse;
    wire host_wr_b = (hmode == H_WRB) & rx_pulse;
    wire host_wr_z = (hmode == H_WRZ) & rx_pulse;

    // ---- coeff / A (BRAM0/1) ----
    wire        a_ren   = eng_run ? e_aren : rdc;
    wire [7:0]  a_raddr = eng_run ? e_araddr : (rdc ? rd_waddr : word_addr);
    wire        a0_wen  = eng_run ? e_awen : (host_wr_a & ~hi);
    wire        a1_wen  = eng_run ? e_awen : (host_wr_a &  hi);
    wire [7:0]  a_waddr = eng_run ? e_awaddr : word_addr;
    wire [7:0]  a0_din  = eng_run ? e_awdata[7:0]  : rx_data;
    wire [7:0]  a1_din  = eng_run ? e_awdata[15:8] : rx_data;

    // ---- twiddle (BRAM2/3) ----
    wire        t_ren   = eng_run ? e_wren : 1'b0;
    wire [7:0]  t_raddr = eng_run ? e_waddr : 8'd0;

    // ---- operand B (BRAM4/5) ----
    wire        b_ren   = eng_run ? e_bren   : 1'b0;
    wire [7:0]  b_raddr = eng_run ? e_braddr : 8'd0;

    // ========================================================================
    //  BRAM port wiring (enables inverted to active-low at the pads)
    // ========================================================================
    // ---- BRAM0 : A low ----
    assign BRAM0_READ_ADDR  = {1'b0, a_raddr};
    assign BRAM0_REN        = ~a_ren;
    assign BRAM0_RCLKEN     = ~a_ren;
    assign BRAM0_WRITE_ADDR = {1'b0, a_waddr};
    assign BRAM0_DATA_IN    = a0_din;
    assign BRAM0_WEN        = ~a0_wen;
    assign BRAM0_WCLKEN     = ~a0_wen;
    // ---- BRAM1 : A high ----
    assign BRAM1_READ_ADDR  = {1'b0, a_raddr};
    assign BRAM1_REN        = ~a_ren;
    assign BRAM1_RCLKEN     = ~a_ren;
    assign BRAM1_WRITE_ADDR = {1'b0, a_waddr};
    assign BRAM1_DATA_IN    = a1_din;
    assign BRAM1_WEN        = ~a1_wen;
    assign BRAM1_WCLKEN     = ~a1_wen;
    // ---- BRAM2 : twiddle low ----
    assign BRAM2_READ_ADDR  = {1'b0, t_raddr};
    assign BRAM2_REN        = ~t_ren;
    assign BRAM2_RCLKEN     = ~t_ren;
    assign BRAM2_WRITE_ADDR = {1'b0, word_addr};
    assign BRAM2_DATA_IN    = rx_data;
    assign BRAM2_WEN        = ~(host_wr_z & ~hi);
    assign BRAM2_WCLKEN     = ~(host_wr_z & ~hi);
    // ---- BRAM3 : twiddle high ----
    assign BRAM3_READ_ADDR  = {1'b0, t_raddr};
    assign BRAM3_REN        = ~t_ren;
    assign BRAM3_RCLKEN     = ~t_ren;
    assign BRAM3_WRITE_ADDR = {1'b0, word_addr};
    assign BRAM3_DATA_IN    = rx_data;
    assign BRAM3_WEN        = ~(host_wr_z & hi);
    assign BRAM3_WCLKEN     = ~(host_wr_z & hi);
    // ---- BRAM4 : B low ----
    // ---- BRAM5 : B high ----

endmodule

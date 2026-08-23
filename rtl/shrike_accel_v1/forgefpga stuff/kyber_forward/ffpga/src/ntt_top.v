// SPI-RX proof v2 -- NO external rst_n (like blink, which works at full brightness).
// LED starts OFF; latches ON when ANY SPI byte is received. led_en tied high.
// If LED goes full-bright after you send a byte -> RX works.
// If LED is half-bright/unresponsive -> the pin isn't being driven cleanly.
(* top *) module ntt_top (
    (* iopad_external_pin, clkbuf_inhibit *) input  clk,
    (* iopad_external_pin *)                 output clk_en,
    (* iopad_external_pin *)                 input  spi_ss_n,
    (* iopad_external_pin *)                 input  spi_sck,
    (* iopad_external_pin *)                 input  spi_mosi,
    (* iopad_external_pin *)                 output spi_miso,
    (* iopad_external_pin *)                 output spi_miso_en,
    (* iopad_external_pin *)                 output reg led,
    (* iopad_external_pin *)                 output led_en
);
    assign clk_en = 1'b1;
    assign led_en = 1'b1;

    // internal reset: hold reset for first 15 clocks after OSC starts, then release.
    reg [3:0] por = 4'd0;
    always @(posedge clk) if (por != 4'd15) por <= por + 4'd1;
    wire rst_n = (por == 4'd15);

    wire [7:0] rx_data; wire rx_valid; wire tx_hold;
    spi_target u_spi(
        .i_clk(clk), .i_rst_n(rst_n), .i_enable(1'b1),
        .i_ss_n(spi_ss_n), .i_sck(spi_sck), .i_mosi(spi_mosi),
        .o_miso(spi_miso), .o_miso_oe(spi_miso_en),
        .o_rx_data(rx_data), .o_rx_data_valid(rx_valid),
        .i_tx_data(rx_data), .o_tx_data_hold(tx_hold)
    );

    always @(posedge clk)
        if (!rst_n)        led <= 1'b0;
        else if (rx_valid) led <= 1'b1;   // sticks ON after first byte
endmodule
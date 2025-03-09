/* Blink LED0 at 1 Hz using internal oscillator */
module top (
    output LED0
);

    reg [31:0] counter = 0;
    reg led_state = 0;

    // Internal oscillator instance
    wire clk;
    SB_HFOSC #(
        .CLKHF_DIV("0b10") // Divide by 4, gives 12 MHz output
    ) internal_oscillator (
        .CLKHFEN(1'b1),
        .CLKHFPU(1'b1),
        .CLKHF(clk)
    );

    always @(posedge clk) begin
        if (counter == 48_000_000) begin // 12 MHz clock divided to 1 Hz
            counter <= 0;
            led_state <= ~led_state;
        end else begin
            counter <= counter + 1;
        end
    end

    assign LED0 = led_state;

endmodule
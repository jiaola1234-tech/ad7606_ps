`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company: 
// Engineer: 
// 
// Create Date: 2025/11/26 18:54:10
// Design Name: 
// Module Name: zero_tap
// Project Name: 
// Target Devices: 
// Tool Versions: 
// Description: 
// 
// Dependencies: 
// 
// Revision:
// Revision 0.01 - File Created
// Additional Comments:
// 
//////////////////////////////////////////////////////////////////////////////////


module zero_tap(
    input  wire        clk,
    input  wire        rst_n,

    input  wire        data_valid,   // 每次输入16bit拉高
    input  wire [15:0] data_in,      

    output reg         frame_valid,  // 256bit输出有效
    output reg [255:0] frame_out      // 拼接后的256bit
);

    reg [255:0] buffer;   // 临时存储8组32bit
    reg [2:0]   cnt;      // 计数0~7

    always @(posedge clk or negedge rst_n) begin
        if(!rst_n) begin
            buffer <= 0;
            cnt <= 0;
            frame_out <= 0;
            frame_valid <= 0;
        end else begin
            frame_valid <= 0;  // 默认无效

            if(data_valid) begin
                // 每次输入16bit，补零成32bit，左移拼入buffer
                buffer <= { buffer[223:0], 16'd0, data_in };
                cnt <= cnt + 1;

                // 当累计8组 → 输出256bit
                if(cnt == 3'd7) begin
                    frame_out <= { buffer[223:0], 16'd0, data_in };
                    frame_valid <= 1;
                    cnt <= 0;  // 重置计数器
                end
            end
        end
    end
endmodule

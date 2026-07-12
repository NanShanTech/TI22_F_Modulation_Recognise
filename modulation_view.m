% modulation_view.m
% 调制信号波形观测脚本
% 参数范围：载波 10~30MHz，调制正弦 1~5kHz，调制度 0.2~1

clear; clc; close all;

%% ================= 参数设置（修改这里即可）=================
fc       = 10e6;      % 载波频率 (Hz)，范围 10e6 ~ 30e6
fm       = 1e3;       % 调制频率 (Hz)，范围 1e3 ~ 5e3
mod_type = 'FM';      % 调制类型：'FM' 'AM' 'CW'
ma       = 0.5;       % 调制度 (AM: 0~1 调幅系数; FM: 频偏比 Δf/fm)

%% ================= 采样参数 =================
fs = 200e6;                    % 采样率 200MHz（奈奎斯特: >2*30MHz=60MHz）
T  = 5 / fm;                   % 显示 5 个调制周期
t  = 0 : 1/fs : T - 1/fs;
N  = length(t);

%% ================= 调制信号生成 =================
mod_sig = sin(2 * pi * fm * t);   % 调制正弦波

switch upper(mod_type)
    case 'AM'
        % s(t) = (1 + ma·sin(2π·fm·t)) · sin(2π·fc·t)
        s = (1 + ma * mod_sig) .* sin(2 * pi * fc * t);
        sig_label = 'AM 调幅';

    case 'FM'
        % s(t) = sin(2π·fc·t + ma·sin(2π·fm·t))
        % ma = Δf / fm，即调制指数
        s = sin(2 * pi * fc * t + ma * mod_sig);
        sig_label = 'FM 调频';

    case 'CW'
        % s(t) = sin(2π·fc·t)  等幅报，未调制载波
        s = sin(2 * pi * fc * t);
        sig_label = 'CW 等幅报';
end

%% ================= 频谱计算 =================
f_axis = (-N/2 : N/2-1) / N * fs;
S      = fftshift(abs(fft(s)) / N);

%% ================= 绘图 =================
figure('Name', '调制信号波形观测', 'Position', [100 100 1200 700]);

% ---- 时域全局（5 个调制周期）----
subplot(3,2,1);
plot(t * 1e6, s, 'b');
xlabel('时间 (\mus)');
ylabel('幅度');
title(sprintf('%s — 全局 (%d 个调制周期)', sig_label, round(T*fm)));
grid on;

% ---- 时域局部（~10 个载波周期）----
subplot(3,2,2);
Tc = 1 / fc;
n_carrier = min(10, round(T / Tc));
n_samp    = min(round(n_carrier * Tc * fs), N);
plot(t(1:n_samp) * 1e6, s(1:n_samp), 'b');
xlabel('时间 (\mus)');
ylabel('幅度');
title(sprintf('载波局部 (%d 个载波周期)', n_carrier));
grid on;

% ---- 频谱全景（±2 倍载频）----
subplot(3,2,[3 4]);
idx_f = f_axis >= -fc*2 & f_axis <= fc*2;
plot(f_axis(idx_f) / 1e6, S(idx_f), 'r');
xlabel('频率 (MHz)');
ylabel('幅度');
title(sprintf('频谱 — 载波 %.0f MHz', fc/1e6));
grid on;

% ---- 调制信号（基带）----
subplot(3,2,5);
plot(t * 1e3, mod_sig, 'm');
xlabel('时间 (ms)');
ylabel('幅度');
title(sprintf('调制信号 (f_m = %.1f kHz)', fm / 1e3));
grid on;

% ---- 信息面板 ----
subplot(3,2,6);
axis off;
y = 0.85;
dy = 0.18;
text(0.05, y,      sprintf('载波频率:  %.2f MHz', fc / 1e6),  'FontSize', 12, 'FontWeight', 'bold');
text(0.05, y - dy, sprintf('调制频率:  %.2f kHz', fm / 1e3),  'FontSize', 12);
text(0.05, y - 2*dy, sprintf('调制类型:  %s',     upper(mod_type)), 'FontSize', 12);
text(0.05, y - 3*dy, sprintf('调制度:    %.2f',   ma),        'FontSize', 12);
if strcmpi(mod_type, 'FM')
    text(0.05, y - 4*dy, sprintf('最大频偏:  %.2f kHz', ma * fm / 1e3), 'FontSize', 12);
end

%% ================= 命令行输出 =================
fprintf('\n========== 信号参数 ==========\n');
fprintf('载波频率: %.2f MHz\n', fc / 1e6);
fprintf('调制频率: %.2f kHz\n', fm / 1e3);
fprintf('调制类型: %s\n',       upper(mod_type));
fprintf('调制度:   %.2f\n',     ma);
fprintf('采样率:   %.0f MHz\n', fs / 1e6);
fprintf('采样点数: %d\n',       N);
fprintf('峰值幅度: %.3f V\n',   max(abs(s)));

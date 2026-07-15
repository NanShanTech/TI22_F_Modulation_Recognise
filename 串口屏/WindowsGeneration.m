%% 自动化窗函数生成器 Pro (带功率检查与多窗支持)
clear; clc;

% --- 1. 图形化选择点数 ---
prompt = {'请输入 FFT 点数 (必须是 2 的次方, 如 256, 1024, 2048):'};
dlgtitle = 'FFT 参数设置';
dims = [1 60];
definput = {'2048'};
answer = inputdlg(prompt, dlgtitle, dims, definput);

if isempty(answer), return; end
N = str2double(answer{1});

% --- 2. 校验点数是否为 2 的次方 ---
% 逻辑：若 log2(N) 是整数，则为 2 的次方
if isnan(N) || N <= 0 || mod(log2(N), 1) ~= 0
    errordlg(['错误：输入的值 ', answer{1}, ' 不是 2 的次方！请重新运行并输入 2^n。'], '输入校验失败');
    return;
end

% --- 3. 图形化选择窗类型 (带中英文注释) ---
win_options = { ...
    'Hanning (汉宁窗 - 最常用，频谱泄露低)', ...
    'Hamming (海明窗 - 旁瓣抑制好)', ...
    'Blackman (布莱克曼窗 - 旁瓣更低，但主瓣宽)', ...
    'Blackman-Harris (布莱克曼-哈里斯窗 - 高动态范围分析)', ...
    'Nuttall (纳托尔窗 - 旁瓣衰减极快)', ...
    'Flat Top (平顶窗 - 幅值测量最准)', ...
    'Rectangular (矩形窗/不加窗 - 频率分辨率最高)'};

[indx, tf] = listdlg('PromptString', '请选择窗函数类型:', ...
    'SelectionMode', 'single', ...
    'ListString', win_options, ...
    'ListSize', [350, 200]);

if ~tf, return; end

% --- 4. 执行窗函数计算 ---
% 获取简短的英文名用于变量命名
switch indx
    case 1, win = hann(N);           prefix = 'Hanning';
    case 2, win = hamming(N);        prefix = 'Hamming';
    case 3, win = blackman(N);       prefix = 'Blackman';
    case 4, win = blackmanharris(N); prefix = 'BlackmanHarris';
    case 5, win = nuttallwin(N);     prefix = 'Nuttall';
    case 6, win = flattopwin(N);     prefix = 'FlatTop';
    case 7, win = rectwin(N);        prefix = 'Rectangular';
end

% --- 5. 生成 C 语言代码 ---
array_name = sprintf('%s_Window_%d', prefix, N);
file_name = [array_name, '.h'];

fid = fopen(file_name, 'wt');
fprintf(fid, '/* \n * 自动生成窗函数数组\n * 类型: %s\n * 点数: %d \n */\n\n', win_options{indx}, N);
fprintf(fid, '#ifndef _%s_H_\n', upper(array_name));
fprintf(fid, '#define _%s_H_\n\n', upper(array_name));
fprintf(fid, '#include "arm_math.h"\n\n');
fprintf(fid, 'const float32_t %s[%d] = {\n', array_name, N);

for i = 1:N
    if mod(i-1, 8) == 0, fprintf(fid, '    '); end
    fprintf(fid, '%.9ff', win(i));
    if i < N, fprintf(fid, ', '); else fprintf(fid, ' '); end
    if mod(i, 8) == 0, fprintf(fid, '\n'); end
end

fprintf(fid, '};\n\n#endif\n');
fclose(fid);

msgbox(['成功生成文件: ', file_name], '完成');
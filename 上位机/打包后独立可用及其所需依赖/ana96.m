function u_opt = ana95(DATA)
% ANA95 - 实时接收LabVIEW数据并进行MUSIC流速分析
%
% 输入参数：
%   DATA - [8 × N] 矩阵，8行代表8个通道，N为采样点数
%   Fs   - 采样率 (Hz)
%
% 调用示例：
%   ANA95(DATA, 50000)
u_opt = 0.0;
% ===== 固定配置参数 =====
persistent fig h1 h2 h3 h4 h5 h6 time_points velocity_points estimated_sources_history
persistent R_prev current_time_offset


Fs = 50000;
% 通道选择配置（固定）
selected_channels = [1,2,3,4,5,6,7,8];  % 选择使用的通道
expected_channels = 8;
sensor_spacing = 0.0605;  % 传感器间距 (m)

% 分析参数配置
analysis_band = [0, 50];  % 分析频带 (Hz)

processing_mode = '0';  % 'diff' 或 'demean' 或 '0'
diff_mode = 'adjacent';
doFilter = false; 
filter_type = 'fir'; 
filter_order = 50;
use_zero_phase = true;

doSymmetricSubtraction = false;
doGaussianSmooth = true;
gaussian_sigma = 1.5;

% 信号源数目控制
auto_estimate_sources = false;
manual_num_sources = 6;
max_sources = 20;
min_sources = 1;
source_estimate_method = 'aic';
eigenvalue_threshold_ratio = 0.01;

min_snr_db = 10; 
base_diag_loading = 1e-3; 
adaptive_loading = true;
smoothing_factor = 0.10;

T_cyc_per_m = 1 / sensor_spacing;
k_range = [-2*pi*T_cyc_per_m, 2*pi*T_cyc_per_m]; 
num_k = 800;

u_range = [0.5, 8]; 
u_trials = linspace(u_range(1), u_range(2), 400);
k_valid_range_rad = [0, 83];

% STFT参数（根据传入的Fs动态计算）
win_len_s = 2   ; 
L = round(Fs * win_len_s); 
noverlap = 0; 
nfft = 2^nextpow2(L);

fprintf('\n=== STFT参数 ===\n');
fprintf('窗长: %.1f 秒 (%d 样本)\n', win_len_s, L);
fprintf('重叠: %d 样本\n', noverlap);
fprintf('NFFT: %d\n', nfft);

% ===== 数据接收和验证 =====
if size(DATA, 1) ~= expected_channels
    error('输入数据应为 [%d × N] 矩阵！当前为 [%d × %d]', expected_channels, size(DATA,1), size(DATA,2));
end

% 转置数据：从 [8×N] 转为 [N×8]
raw_data = DATA';  % 现在是 [N × 8]
Ntotal = size(raw_data, 1);

fprintf('\n=== 接收LabVIEW数据 ===\n');
fprintf('数据维度: %d × %d (样本数 × 通道数)\n', Ntotal, expected_channels);
fprintf('采样率: %d Hz\n', Fs);
fprintf('数据时长: %.2f 秒\n', Ntotal/Fs);

% ===== 初始化（仅第一次调用） =====
if isempty(fig) || ~isvalid(fig)
    fprintf('初始化显示窗口...\n');
    
    % 初始化持久变量
    time_points = [];
    velocity_points = [];
    estimated_sources_history = [];
    R_prev = [];
    current_time_offset = 0;
    
    % 创建适合嵌入上位机右侧栏的figure窗口：主脊线图优先，其它图作为辅助视图。
    fig = figure('Position',[50,50,1000,900], 'Name', 'ANA95 实时流速分析', 'NumberTitle', 'off', 'Color', 'w');
    
    h3 = axes('Parent', fig, 'Position', [0.08, 0.54, 0.86, 0.40]);
    title('主脊线图：高斯平滑后 k-f 谱'); xlabel('k (rad/m)'); ylabel('f (Hz)');
    
    h4 = axes('Parent', fig, 'Position', [0.72, 0.32, 0.22, 0.15]);
    title('能量积分'); xlabel('u (m/s)'); ylabel('能量'); grid on;
    
    h1 = axes('Parent', fig, 'Position', [0.08, 0.32, 0.26, 0.15]);
    title('原始MUSIC'); xlabel('k'); ylabel('f');
    
    h2 = axes('Parent', fig, 'Position', [0.40, 0.32, 0.26, 0.15]);
    title('对称相减'); xlabel('k'); ylabel('f');
    
    h6 = axes('Parent', fig, 'Position', [0.72, 0.08, 0.22, 0.17]);
    title('信号源数'); xlabel('t'); ylabel('N'); grid on;
    
    h5 = axes('Parent', fig, 'Position', [0.08, 0.08, 0.58, 0.17]);
    title('流速随时间变化'); xlabel('t (s)'); ylabel('u (m/s)'); grid on;
end

% ===== 通道选择 =====
if max(selected_channels) > expected_channels || min(selected_channels) < 1
    error('选择的通道超出范围！可选通道范围：1-%d', expected_channels);
end

selected_data = raw_data(:, selected_channels);
num_selected_channels = length(selected_channels);

% 计算选择通道的物理位置
min_channel_idx = min(selected_channels);
selected_positions = (selected_channels - min_channel_idx) * sensor_spacing;

% 获取处理后通道信息
[num_proc_channels, proc_positions] = get_processing_info(processing_mode, diff_mode, ...
    selected_positions, num_selected_channels);

% ===== 频率轴设置 =====
f_axis_full = (0:nfft/2) * Fs / nfft;
f_mask = (f_axis_full >= analysis_band(1)) & (f_axis_full <= analysis_band(2));
f_axis = f_axis_full(f_mask);
Fcnt = length(f_axis);

k_axis = linspace(k_range(1), k_range(2), num_k);

% ===== 数据预处理 =====
fprintf('\n=== 数据预处理 ===\n');
proc_data = preprocess_data(selected_data, processing_mode, diff_mode, num_selected_channels);
fprintf('预处理后数据维度: %d × %d\n', size(proc_data,1), size(proc_data,2));

filt_data = apply_filter(proc_data, doFilter, filter_type, filter_order, analysis_band, Fs, use_zero_phase);
fprintf('滤波后数据维度: %d × %d\n', size(filt_data,1), size(filt_data,2));

% 直接使用全部数据进行分析
analysis_data = filt_data;
fprintf('分析数据长度: %.2f 秒 (%d 样本)\n', size(analysis_data,1)/Fs, size(analysis_data,1));

% ===== MUSIC分析 =====
fprintf('\n=== 检查分析条件 ===\n');
fprintf('analysis_data长度: %d 样本\n', size(analysis_data, 1));
fprintf('要求最小长度 L: %d 样本\n', L);
fprintf('条件满足: %s\n', mat2str(size(analysis_data, 1) >= L));

if size(analysis_data, 1) >= L
    fprintf('\n=== 开始MUSIC分析 ===\n');
    
    % STFT变换
    S_list = cell(1, num_proc_channels);
    for c = 1:num_proc_channels
        [S_temp, ~, ~] = spectrogram(analysis_data(:,c), L, noverlap, nfft, Fs);
        S_list{c} = S_temp(f_mask, :);
    end
    
    fprintf('STFT时间切片数 Tstft = %d (数据长度/窗长 ≈ %.1f秒/%.1f秒)\n', ...
            size(S_list{1},2), size(analysis_data,1)/Fs, L/Fs);
    
    if ~isempty(S_list{1})
        Tstft = size(S_list{1}, 2);
        
        R_now_all = zeros(num_proc_channels, num_proc_channels, Fcnt);
        P_music = zeros(Fcnt, num_k);
        num_sources_freq = zeros(Fcnt, 1);
        
        % 对每个频率进行MUSIC分析
        for fi = 1:Fcnt
            X = zeros(num_proc_channels, Tstft);
            for c = 1:num_proc_channels
                X(c,:) = S_list{c}(fi, :);
            end
            
            Rw = (X * X') / Tstft;
            
            % 平滑协方差矩阵
            if ~isempty(R_prev) && size(R_prev,3) >= fi
                Rw = (1 - smoothing_factor) * Rw + smoothing_factor * R_prev(:,:,fi);
            end
            
            % 对角加载
            if adaptive_loading
                sp = trace(Rw)/num_proc_channels;
                ev = sort(real(eig(Rw)),'descend');
                if numel(ev) >= 2
                    np = mean(ev(max(1,end-min(2,floor(num_proc_channels/3))):end));
                    snr_est = 10*log10(sp/(np+eps));
                    dl = base_diag_loading * (snr_est<min_snr_db)*(min_snr_db - snr_est + 1) + ...
                         base_diag_loading * (snr_est>=min_snr_db);
                else
                    dl = base_diag_loading;
                end
            else
                dl = base_diag_loading;
            end
            
            Rw = 0.5*(Rw+Rw'); 
            Rw = Rw + dl * trace(Rw) * eye(num_proc_channels);
            
            % 特征分解
            [V, D] = eig(Rw);
            eigenvals = sort(real(diag(D)), 'descend');
            [eigenvals_sorted, idx] = sort(real(diag(D)), 'descend');
            V = V(:, idx);
            
            % 估计信号源数目
            if auto_estimate_sources
                num_sources = estimate_num_sources(eigenvals, Tstft, num_proc_channels, ...
                    source_estimate_method, max_sources, min_sources, eigenvalue_threshold_ratio);
            else
                num_sources = manual_num_sources;
            end
            num_sources_freq(fi) = num_sources;
            
            % 噪声子空间
            noise_subspace = V(:, num_sources+1:end);
            
            % MUSIC谱
            for ki = 1:num_k
                a = compute_steering_vector(k_axis(ki), proc_positions, processing_mode, ...
                    diff_mode, selected_positions, sensor_spacing);
                a = a / norm(a);
                P_music(fi,ki) = 1 / real(a' * (noise_subspace * noise_subspace') * a + eps);
            end
            
            R_now_all(:,:,fi) = Rw;
        end
        
        estimated_sources = round(median(num_sources_freq));
        estimated_sources = max(min_sources, min(estimated_sources, max_sources));
        
        R_prev = R_now_all;
        
        % 后处理MUSIC谱
        P_db = 10*log10(abs(P_music)+eps);
        P_db = medfilt2(P_db, [3,3]);
        P_original = P_db;
        
        if doSymmetricSubtraction
            P_as = zeros(size(P_db));
            for fi = 1:Fcnt
                s = P_db(fi,:);
                for ki = 1:num_k
                    kv = k_axis(ki);
                    [~, mir] = min(abs(k_axis + kv));
                    P_as(fi,ki) = (s(ki) - s(mir))/2;
                end
            end
            mn = min(P_as(P_as>=0)); 
            P_as(P_as<0) = mn;
            th = 0; 
            P_as(P_as<th) = th;
            P_after_subtract = P_as;
        else
            P_after_subtract = P_db;
        end
        
        if doGaussianSmooth
            kernel_size = max(3, round(6*gaussian_sigma));
            if mod(kernel_size, 2) == 0, kernel_size = kernel_size + 1; end
            
            [X, Y] = meshgrid(-(kernel_size-1)/2:(kernel_size-1)/2);
            gaussian_kernel = exp(-(X.^2 + Y.^2)/(2*gaussian_sigma^2));
            gaussian_kernel = gaussian_kernel / sum(gaussian_kernel(:));
            
            P_smoothed = conv2(P_after_subtract, gaussian_kernel, 'same');
            P_use = P_smoothed;
        else
            P_use = P_after_subtract;
        end
        
        % 倾斜叠加法估计流速（修正：与导向矢量符号一致）
        energy = zeros(length(u_trials),1);
        for ui = 1:length(u_trials)
            u = u_trials(ui);
            k_line = (2*pi * f_axis) / u;  % 保持正号，与导向矢量一致
            le = 0; cnt = 0;
            for fi = 1:Fcnt
                kv = k_line(fi);
                if kv>=k_valid_range_rad(1) && kv<=k_valid_range_rad(2) && kv>=min(k_axis) && kv<=max(k_axis)
                    val = interp1(k_axis, P_use(fi,:).', kv, 'spline', 0);
                    le = le + val; cnt = cnt + 1;
                end
            end
            if cnt>0, energy(ui) = le/cnt; end
        end
        
        en_s = movmean(energy,3);
        [pk, loc] = findpeaks(en_s);
        if isempty(loc), [~, loc] = max(en_s); end
        [~, mxi] = max(en_s(loc)); 
        sel = loc(mxi);
        u_opt = u_trials(sel);
        
        % 精细搜索
        uf = linspace(max(u_opt*0.95,u_range(1)), min(u_opt*1.05,u_range(2)), 50);
        enf = interp1(u_trials, energy, uf, 'spline'); 
        [~, ix] = max(enf); 
        u_opt = uf(ix);
        
        % 更新时间和结果
        data_duration_actual = size(analysis_data,1) / Fs;  % 实际数据时长
        current_time = current_time_offset + data_duration_actual / 2;
        time_points(end+1) = current_time;
        velocity_points(end+1) = u_opt;
        estimated_sources_history(end+1) = estimated_sources;
        
        fprintf('\n=== 分析结果 ===\n');
        fprintf('时间: %.1f s, 流速: %.2f m/s, 信号源数: %d\n', ...
                current_time, u_opt, estimated_sources);
        
        % 更新显示
        figure(fig);
        
        axes(h1); cla; 
        imagesc(k_axis, f_axis, P_original); 
        set(gca,'YDir','normal'); colormap(jet);
        hold on; plot((2*pi*f_axis)/u_opt, f_axis, 'r--','LineWidth',2);  % 保持正号
        xlabel('k (rad/m)'); ylabel('f (Hz)'); 
        title(sprintf('原始MUSIC (Sources=%d, t=%.1f s)', estimated_sources, current_time));
        
        axes(h2); cla; 
        imagesc(k_axis, f_axis, P_after_subtract); 
        set(gca,'YDir','normal'); colormap(jet);
        hold on; plot((2*pi*f_axis)/u_opt, f_axis, 'r--','LineWidth',2);  % 保持正号
        xlabel('k (rad/m)'); ylabel('f (Hz)'); 
        title(sprintf('对称相减后 (t=%.1f s)', current_time));
        
        axes(h3); cla; 
        imagesc(k_axis, f_axis, P_use); 
        set(gca,'YDir','normal'); colormap(jet); colorbar;
        hold on; plot((2*pi*f_axis)/u_opt, f_axis, 'r--','LineWidth',2);  % 保持正号
        xlabel('k (rad/m)'); ylabel('f (Hz)'); 
        title(sprintf('主脊线图：高斯平滑 k-f 谱 (u=%.2f m/s, t=%.1f s)', u_opt, current_time));
        
        axes(h4); cla; 
        plot(u_trials, energy, 'b-','LineWidth',1.5); hold on;
        plot(u_opt, interp1(u_trials,energy,u_opt,'spline'), 'ro','MarkerSize',9,'LineWidth',2);
        xlabel('u (m/s)'); ylabel('累计能量'); 
        title(sprintf('能量积分, u_{opt}=%.2f m/s', u_opt)); grid on;
        
        axes(h6); cla;
        plot(time_points, estimated_sources_history, 'go-','LineWidth',1.5,'MarkerSize',6);
        xlabel('t (s)'); ylabel('信号源数'); title('估计信号源数随时间变化'); grid on;
        ylim([0, max_sources+1]);
        
        axes(h5); cla; 
        plot(time_points, velocity_points, 'bo-','LineWidth',1.5,'MarkerSize',6);
        xlabel('t (s)'); ylabel('u (m/s)'); title('流速随时间变化'); grid on;
        if length(time_points) > 1
            xlim([min(time_points), max(time_points)]);
        end
        ylim([0.5, max(5, max(velocity_points)*1.2)]);
        
        drawnow;
    end
else
    fprintf('数据长度不足 (%.2f秒 < %.2f秒)，跳过分析\n', size(analysis_data,1)/Fs, L/Fs);
end

% 更新时间偏移（累积已处理的数据时长）
current_time_offset = current_time_offset + Ntotal/Fs;

fprintf('\n=== 本次调用完成 ===\n');
fprintf('累计处理时间: %.1f 秒\n', current_time_offset);

end

% ===== 辅助函数 =====

function num_proc = get_num_proc_channels(processing_mode, diff_mode, num_selected)
    switch lower(processing_mode)
        case 'diff'
            if strcmp(diff_mode, 'adjacent')
                num_proc = num_selected - 1;
            else
                error('未知差分模式');
            end
        otherwise
            num_proc = num_selected;
    end
end

function num_sources = estimate_num_sources(eigenvals, N, M, method, max_sources, min_sources, threshold_ratio)
    eigenvals = real(eigenvals);
    eigenvals = eigenvals(eigenvals > 0);
    
    max_testable = min(max_sources, min(M-1, length(eigenvals)-1));
    
    switch lower(method)
        case 'aic'
            aic_scores = zeros(max_testable, 1);
            for k = min_sources:max_testable
                noise_var = mean(eigenvals(k+1:end));
                log_likelihood = -N * (M-k) * log(noise_var) - N * sum(eigenvals(k+1:end)) / noise_var;
                aic_scores(k) = -2 * log_likelihood + 2 * k * (2*M - k);
            end
            [~, num_sources] = min(aic_scores(min_sources:max_testable));
            num_sources = num_sources + min_sources - 1;
            
        case 'mdl'
            mdl_scores = zeros(max_testable, 1);
            for k = min_sources:max_testable
                noise_var = mean(eigenvals(k+1:end));
                log_likelihood = -N * (M-k) * log(noise_var) - N * sum(eigenvals(k+1:end)) / noise_var;
                mdl_scores(k) = -log_likelihood + 0.5 * k * (2*M - k) * log(N);
            end
            [~, num_sources] = min(mdl_scores(min_sources:max_testable));
            num_sources = num_sources + min_sources - 1;
            
        case 'eigenratio'
            if length(eigenvals) < 2
                num_sources = min_sources;
                return;
            end
            threshold = eigenvals(1) * threshold_ratio;
            significant_idx = find(eigenvals >= threshold, 1, 'last');
            if isempty(significant_idx)
                num_sources = min_sources;
            else
                num_sources = min(max_testable, max(min_sources, significant_idx));
            end
            
        case 'adaptive'
            if length(eigenvals) < 2
                num_sources = min_sources;
                return;
            end
            
            threshold = eigenvals(1) * threshold_ratio;
            eigen_estimate = find(eigenvals >= threshold, 1, 'last');
            if isempty(eigen_estimate), eigen_estimate = min_sources; end
            
            aic_scores = zeros(max_testable, 1);
            for k = min_sources:max_testable
                noise_var = mean(eigenvals(k+1:end));
                log_likelihood = -N * (M-k) * log(noise_var) - N * sum(eigenvals(k+1:end)) / noise_var;
                aic_scores(k) = -2 * log_likelihood + 2 * k * (2*M - k);
            end
            [~, aic_estimate] = min(aic_scores(min_sources:max_testable));
            aic_estimate = aic_estimate + min_sources - 1;
            
            num_sources = round((eigen_estimate + aic_estimate) / 2);
            num_sources = min(max_testable, max(min_sources, num_sources));
            
        otherwise
            num_sources = min(max_sources, min_sources);
    end
    
    num_sources = min(max_testable, max(min_sources, num_sources));
end

function proc_data = preprocess_data(raw_chunk, processing_mode, diff_mode, num_channels)
    switch lower(processing_mode)
        case 'diff'
            if strcmp(diff_mode,'adjacent')
                M = num_channels - 1;
                proc_data = zeros(size(raw_chunk,1), M);
                for i = 1:M
                    proc_data(:,i) = raw_chunk(:,i+1) - raw_chunk(:,i);
                end
            else
                error('未知差分模式');
            end
        case 'demean'
            proc_data = raw_chunk - mean(raw_chunk,2);
        otherwise
            proc_data = raw_chunk;
    end
end

function filtered_data = apply_filter(data, doFilter, filter_type, filter_order, analysis_band, Fs, use_zero_phase)
    if doFilter
        switch lower(filter_type)
            case 'fir'
                b = fir1(filter_order, analysis_band(2)/(Fs/2), 'low');
                if use_zero_phase
                    filtered_data = filtfilt(b, 1, data);
                else
                    filtered_data = filter(b, 1, data);
                end
            case 'iir'
                [b,a] = butter(4, analysis_band(2)/(Fs/2), 'low');
                if use_zero_phase
                    filtered_data = filtfilt(b, a, data);
                else
                    filtered_data = filter(b, a, data);
                end
            otherwise
                filtered_data = data;
        end
    else
        filtered_data = data;
    end
end

function [num_proc_channels, proc_positions] = get_processing_info(processing_mode, diff_mode, selected_positions, num_selected_channels)
    switch lower(processing_mode)
        case 'diff'
            if strcmp(diff_mode, 'adjacent')
                num_proc_channels = num_selected_channels - 1;
                proc_positions = (selected_positions(1:end-1) + selected_positions(2:end)) / 2;
            else
                error('未知差分模式');
            end
        case 'demean'
            num_proc_channels = num_selected_channels;
            proc_positions = selected_positions;
        otherwise
            num_proc_channels = num_selected_channels;
            proc_positions = selected_positions;
    end
end

function a = compute_steering_vector(k, proc_positions, processing_mode, diff_mode, selected_positions, sensor_spacing)
    switch lower(processing_mode)
        case 'diff'
            if strcmp(diff_mode, 'adjacent')
                base_steering = exp(1j * k * selected_positions);  % 去掉负号，反转k方向
                diff_vector = base_steering(2:end) - base_steering(1:end-1);
                a = diff_vector(:);
            else
                error('未知差分模式');
            end
        case 'demean'
            a = exp(1j * k * proc_positions(:));  % 去掉负号，反转k方向
        otherwise
            a = exp(1j * k * proc_positions(:));  % 去掉负号，反转k方向
    end
end
% 
% % 测试代码
% clear all;
% close all;
% clc;
% 
% % 生成测试数据：20秒，8通道
% Fs = 50000;
% duration = 20;  
% N = Fs * duration;  % 1,000,000 样本
% 
% % 生成模拟流速信号（带有k-f结构）
% t = (0:N-1)/Fs;
% u_true = 2.5;  % 真实流速 2.5 m/s
% f_test = 10:5:40;  % 测试频率 10, 15, 20, 25, 30, 35, 40 Hz
% sensor_spacing = 0.0605;
% 
% DATA = zeros(8, N);
% for ch = 1:8
%     pos = (ch-1) * sensor_spacing;  % 传感器位置
%     signal = zeros(1, N);
%     for f = f_test
%         k = 2*pi*f / u_true;  % 对应的波数
%         signal = signal + sin(2*pi*f*t - k*pos);
%     end
%     DATA(ch, :) = signal + 0.5*randn(1, N);  % 加噪声
% end
% 
% fprintf('测试数据生成完成：\n');
% fprintf('  数据维度: %d × %d\n', size(DATA,1), size(DATA,2));
% fprintf('  采样率: %d Hz\n', Fs);
% fprintf('  时长: %.1f 秒\n', N/Fs);
% fprintf('  真实流速: %.2f m/s\n\n', u_true);
% 
% % 调用函数
% ana95(DATA);

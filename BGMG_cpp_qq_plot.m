function [figures, plot_data] = BGMG_cpp_qq_plot(params, zvec, options)
    % QQ plot for data and model

    if ~isfield(options, 'title'), options.title = 'UNKNOWN TRAIT'; end;
    plot_data = []; figures.tot = figure; hold on;

    % Retrieve weights from c++ library
    check = @()fprintf('RESULT: %s; STATUS: %s\n', calllib('bgmg', 'bgmg_get_last_error'), calllib('bgmg', 'bgmg_status', 0));
    pBuffer = libpointer('singlePtr', zeros(length(zvec), 1, 'single'));
    calllib('bgmg', 'bgmg_retrieve_weights', 0, length(zvec), pBuffer);  check(); 
    weights_bgmg = pBuffer.Value;
    clear pBuffer
    
    % Check that weights and zvec are all defined
    if any(~isfinite(zvec + weights_bgmg)), error('all values must be defined'); end;
    weights = weights_bgmg ./ sum(weights_bgmg);
    
    % Calculate data_logpvec
    hv_z = linspace(0, min(max(abs(zvec)), 38.0), 10000);
    [data_y, si] = sort(-log10(2*normcdf(-abs(zvec))));
    data_x=-log10(cumsum(weights(si),1,'reverse'));
    data_idx = ([data_y(2:end); +Inf] ~= data_y);
    hv_logp = -log10(2*normcdf(-hv_z));
    data_logpvec = interp1(data_y(data_idx), data_x(data_idx), hv_logp);

    % Calculate model_logpvec
    zgrid = single(0:0.05:15); 
    pBuffer = libpointer('singlePtr', zeros(length(zgrid), 1, 'single'));
    calllib('bgmg', 'bgmg_calc_univariate_pdf', 0, params.pi_vec, params.sig2_zero, params.sig2_beta, length(zgrid), zgrid, pBuffer);  check(); 
    pdf = pBuffer.Value'; clear pBuffer
    pdf = pdf / sum(weights_bgmg);
    if (zgrid(1) == 0), zgrid = [-fliplr(zgrid(2:end)) zgrid];pdf = [fliplr(pdf(2:end)) pdf]; end
    model_cdf = cumsum(pdf)  * (zgrid(2) - zgrid(1)) ;
    X = model_cdf;X1 = ones(size(X, 1), 1); X0 = zeros(size(X, 1), 1);
    model_cdf = 0.5 * ([X0, X(:, 1:(end-1))] + [X(:, 1:(end-1)), X1]);
    model_logpvec = -log10(2*interp1(-zgrid(zgrid<=0), model_cdf(zgrid<=0), hv_z')); % hv_z is to fine, can't afford calculation on it - do interpolation instead; don't matter for QQ plot (no visual difference), but lamGCfromQQ doesn't work for z_grid (to coarse)

    hData  = plot(data_logpvec, hv_logp, '-', 'LineWidth',1); hold on;
    hModel = plot(model_logpvec,hv_logp, '-', 'LineWidth',1); hold on;

    plot_data.hv_logp = hv_logp;
    plot_data.data_logpvec = data_logpvec;
    plot_data.model_logpvec = model_logpvec;
    plot_data.params = params;
    plot_data.options = options;
    plot_data.options.calculate_z_cdf_weights='removed';
    plot_data.options.mafvec='removed';

    qq_options = params; % must be on the top of other lines, otherwise this assigment overwrites all qq_options
    qq_options.title = options.title;
    if isfield(params, 'pi_vec')
        qq_options.h2 = sum(qq_options.pi_vec.*qq_options.sig2_beta)*options.total_het;
        if length(qq_options.pi_vec) > 1
            qq_options.h2vec = (qq_options.pi_vec.*qq_options.sig2_beta) *options.total_het;
        end
    end
    
    qq_options.lamGC_data = BGMG_util.lamGCfromQQ(data_logpvec, hv_logp);
    qq_options.lamGC_model = BGMG_util.lamGCfromQQ(model_logpvec, hv_logp);
    qq_options.n_snps = length(zvec);

    annotate_qq_plot(qq_options);
end

function annotate_qq_plot(qq_options)
    % Tune appearance of the QQ plot, put annotations, title, etc...
    if ~isfield(qq_options, 'qqlimy'), qq_options.qqlimy = 20; end;
    if ~isfield(qq_options, 'qqlimx'), qq_options.qqlimx = 7; end;
    if ~isfield(qq_options, 'fontsize'), qq_options.fontsize = 19; end;
    if ~isfield(qq_options, 'legend'), qq_options.legend = true; end;
    if ~isfield(qq_options, 'xlabel'), qq_options.xlabel = true; end;
    if ~isfield(qq_options, 'ylabel'), qq_options.ylabel = true; end;

    has_opt = @(opt)(isfield(qq_options, opt));

    plot([0 qq_options.qqlimy],[0 qq_options.qqlimy], 'k--');
    xlim([0 qq_options.qqlimx]); ylim([0 qq_options.qqlimy]);
    if has_opt('legend') && qq_options.legend, lgd=legend('Data', 'Model', 'Expected', 'Location', 'SouthEast'); lgd.FontSize = qq_options.fontsize; end;
    if has_opt('xlabel') && qq_options.xlabel, xlabel('Empirical -log 10(q)','fontsize',qq_options.fontsize); end;
    if has_opt('ylabel') && qq_options.ylabel, ylabel('Nominal -log 10(p)','fontsize',qq_options.fontsize); end;
    if has_opt('title'), title(qq_options.title,'fontsize',qq_options.fontsize,'Interpreter','latex'); end;
    xt = get(gca, 'XTick');set(gca, 'FontSize', qq_options.fontsize);
    yt = get(gca, 'YTick');set(gca, 'FontSize', qq_options.fontsize);
    set(gca, 'box','off');

    loc = qq_options.qqlimy-2;
    if has_opt('n_snps'), text(0.5,loc,sprintf('$$ n_{snps} = %i $$', qq_options.n_snps),'FontSize',qq_options.fontsize,'Interpreter','latex'); loc = loc - 2; end;
    if has_opt('sig2_zero'), text(0.5,loc,sprintf('$$ \\hat\\sigma_0^2 = %.3f $$', qq_options.sig2_zero),'FontSize',qq_options.fontsize,'Interpreter','latex'); loc = loc - 2; end;
    if has_opt('pi_vec'), text(0.5,loc,sprintf('$$ \\hat\\pi^u_1 = %s $$', vec2str(qq_options.pi_vec)),'FontSize',qq_options.fontsize,'Interpreter','latex'); loc = loc - 2; end;
    if has_opt('sig2_beta'), text(0.5,loc,sprintf('$$ \\hat\\sigma_{\\beta}^2 = %s $$', vec2str(qq_options.sig2_beta)),'FontSize',qq_options.fontsize,'Interpreter','latex'); loc = loc - 2; end;
    h2vec = ''; if has_opt('h2vec'), h2vec = ['$$ \\; $$' vec2str(qq_options.h2vec, 3)]; end;
    if has_opt('h2'), text(0.5,loc,sprintf('$$ \\hat h^2 = %.3f%s$$', qq_options.h2, h2vec),'FontSize',qq_options.fontsize,'Interpreter','latex'); loc = loc - 2; end;
    if has_opt('lamGC_model'), text(0.5,loc,sprintf('$$ \\hat\\lambda_{model} = %.3f $$', qq_options.lamGC_model),'FontSize',qq_options.fontsize,'Interpreter','latex'); loc = loc - 2; end;
    if has_opt('lamGC_data'), text(0.5,loc,sprintf('$$ \\hat\\lambda_{data} = %.3f $$', qq_options.lamGC_data),'FontSize',qq_options.fontsize,'Interpreter','latex'); loc = loc - 2; end;
end

function s = vec2str(vec, digits)
    if ~exist('digits', 'var'), digits = 6; end;
    format = ['%.', int2str(digits), 'f'];
    if length(vec) == 1
        s=sprintf(format, vec);
    else
        s=['[', sprintf(format, vec(1))];
        for i=2:length(vec)
            s=[s, ', ', sprintf(format, vec(i))];
        end
        s = [s ']'];
    end
end

#include <core/Stripes.h>

namespace core {

    /* AUXILIARY FUNCTIONS */

    template <typename T>
    uint16_t Stripes<T>::computeStripesBitsPE(uint8_t layer_prec) {
        return layer_prec * (uint8_t)16;
    }

    /* CYCLES */

    template <typename T>
    void Stripes<T>::computeConvolution(const core::Layer<T> &layer, sys::Statistics::Stats &stats) {

        std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();

        cnpy::Array<T> act = layer.getActivations();
        cnpy::Array<T> wgt = layer.getWeights();
        if(wgt.getDimensions() == 2) wgt.reshape_to_4D();

        int padding = layer.getPadding();
        int stride = layer.getStride();

        act.zero_pad(padding);

        if(act.getShape()[1] == 3 && stride > 1) {
            act.reshape_first_layer_act((uint16_t)stride);
            wgt.reshape_first_layer_wgt((uint16_t)stride);
            stride = 1;
        }

        const std::vector<size_t> &act_shape = act.getShape();
        const std::vector<size_t> &wgt_shape = wgt.getShape();

        int batch_size = 1;
        int act_channels = act_shape[1];
        int Nx = act_shape[2];
        int Ny = act_shape[3];

        int num_filters = wgt_shape[0];
        int wgt_channels = wgt_shape[1];
        int Kx = wgt_shape[2];
        int Ky = wgt_shape[3];

        long out_x = (Nx - Kx)/stride + 1;
        long out_y = (Ny - Ky)/stride + 1;

        // Get layer precision
        auto act_layer_prec = layer.getAct_precision();
        auto wgt_layer_prec = layer.getWgt_precision();

        auto columns_per_act = (int)ceil(act_layer_prec / (double)BITS_PE);
        auto rows_per_wgt = (int)ceil(wgt_layer_prec / (double)BITS_PE);
        auto windows_per_tile = N_COLUMNS/columns_per_act;
        auto filters_per_tile = N_ROWS/rows_per_wgt;

        int groups = act_channels / wgt_channels;
        auto num_filters_sets = (uint32_t)ceil(num_filters/(double)filters_per_tile/groups);
        auto baseline_filters_sets = (uint32_t)ceil(num_filters/(double)N_ROWS/groups);

        // Stats
        stats.cycles.emplace_back(std::vector<uint64_t>(batch_size,0));
        stats.idle_columns.push_back((uint64_t)(N_COLUMNS - windows_per_tile*columns_per_act));
        stats.idle_rows.push_back((uint64_t)(N_ROWS - filters_per_tile*rows_per_wgt));
        stats.columns_per_act.push_back((uint64_t)columns_per_act);
        stats.rows_per_wgt.push_back((uint64_t)rows_per_wgt);

        // Convolution
        for(int n=0; n<batch_size; n++) {

            std::vector<int> list_x, list_y;
            int x_counter = 0, y_counter = 0;
            auto precision_cycles = (columns_per_act == 1 && rows_per_wgt == 1) ? act_layer_prec : BITS_PE;

            while(this->iterateWindows(out_x,out_y,list_x,list_y,x_counter, y_counter, windows_per_tile)) {
                for (int i = 0; i < Kx; i++) {
                    for (int j = 0; j < Ky; j++) {
                        for (int k = 0; k < act_channels; k += WEIGHT_LANES) {
                            stats.cycles.back()[n] += precision_cycles;
                        }
                    }
                }
            }
            stats.cycles.back()[n] *= num_filters_sets;
        }

        auto base_cycles = (uint64_t)(out_x * out_y * ceil(act_channels/(double)WEIGHT_LANES) * Kx * Ky *
                baseline_filters_sets);

        std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1);

        stats.time.push_back(time_span);
        stats.baseline_cycles.push_back(base_cycles);

    }

    template <typename T>
    void Stripes<T>::computeConvolution2D(const core::Layer<T> &layer, sys::Statistics::Stats &stats) {

        std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();

        cnpy::Array<T> act = layer.getActivations();
        act.powers_of_two_representation();
        cnpy::Array<T> wgt = layer.getWeights();
        if(wgt.getDimensions() == 2) wgt.reshape_to_4D();

        int padding = layer.getPadding();
        int stride = layer.getStride();

        act.zero_pad(padding);

        const std::vector<size_t> &act_shape = act.getShape();
        const std::vector<size_t> &wgt_shape = wgt.getShape();

        int batch_size = act_shape[0];
        int Nx = act_shape[2];
        int Ny = act_shape[3];
        if(this->FAST_MODE) batch_size = 1;

        int num_filters = wgt_shape[0];
        int wgt_channels = wgt_shape[1];
        int Kx = wgt_shape[2];
        int Ky = wgt_shape[3];

        long out_x = (Nx - Kx)/stride + 1;
        long out_y = (Ny - Ky)/stride + 1;

        // Get layer precision
        auto act_layer_prec = layer.getAct_precision();
        auto wgt_layer_prec = layer.getWgt_precision();

        auto columns_per_act = (int)ceil(act_layer_prec / (double)BITS_PE);
        auto rows_per_wgt = (int)ceil(wgt_layer_prec / (double)BITS_PE);
        if(columns_per_act > rows_per_wgt){
            auto tmp = rows_per_wgt;
            rows_per_wgt = columns_per_act;
            columns_per_act = tmp;
        }
        auto windows_per_tile = N_COLUMNS/columns_per_act;
        auto filters_per_tile = N_ROWS/rows_per_wgt;

        // Stats
        stats.cycles.emplace_back(std::vector<uint64_t>(batch_size,0));
        stats.idle_columns.push_back((uint64_t)(N_COLUMNS - windows_per_tile*columns_per_act));
        stats.idle_rows.push_back((uint64_t)(N_ROWS - filters_per_tile*rows_per_wgt));
        stats.columns_per_act.push_back((uint64_t)columns_per_act);
        stats.rows_per_wgt.push_back((uint64_t)rows_per_wgt);

        int n;

        // Convolution
        #ifdef OPENMP
        auto max_threads = omp_get_max_threads();
        omp_set_num_threads(std::min(max_threads,this->N_THREADS));
        #pragma omp parallel for private(n)
        #endif
        for(n=0; n<batch_size; n++) {

            std::vector<int> list_x, list_y;
            int x_counter = 0, y_counter = 0;
            auto precision_cycles = (columns_per_act == 1 && rows_per_wgt == 1) ? act_layer_prec : BITS_PE;

            for(int m=0; m<num_filters; m+=filters_per_tile) {
                while(this->iterateWindows(out_x,out_y,list_x,list_y,x_counter,y_counter,windows_per_tile)) {
                    for (int i = 0; i < Kx; i++) {
                        for (int j = 0; j < Ky; j++) {
                            for (int k = 0; k < wgt_channels; k+=WEIGHT_LANES) {
                                stats.cycles.back()[n] += precision_cycles;
                            }
                        }
                    }
                }
            }
        }

        auto base_cycles = (uint64_t)(out_x * out_y * ceil(wgt_channels/(double)WEIGHT_LANES) * Kx * Ky *
                ceil(num_filters/(double)N_ROWS));

        std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1);

        stats.time.push_back(time_span);
        stats.baseline_cycles.push_back(base_cycles);

    }


    template <typename T>
    void Stripes<T>::computeInnerProduct(const Layer<T> &layer, sys::Statistics::Stats &stats) {

        std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();

        cnpy::Array<T> act = layer.getActivations();

        if(layer.getType() == "InnerProduct") {
            if (act.getDimensions() == 4) act.reshape_to_2D();
            act.reshape_to_4D();
        }

        const std::vector<size_t> &act_shape = act.getShape();
        const std::vector<size_t> &wgt_shape = layer.getWeights().getShape();

        int batch_size = 1;
        int act_channels, R;
        if(layer.getType() == "LSTM") {
            R = act_shape[0];
            act_channels = act_shape[2];
        } else {
            R = 1;
            act_channels = act_shape[1];
        }
        int num_filters = wgt_shape[0];

        // Get layer precision
        auto act_layer_prec = layer.getAct_precision();
        auto wgt_layer_prec = layer.getWgt_precision();

        auto columns_per_act = (int)ceil(act_layer_prec / (double)BITS_PE);
        auto rows_per_wgt = (int)ceil(wgt_layer_prec / (double)BITS_PE);
        if(columns_per_act > rows_per_wgt){
            auto tmp = rows_per_wgt;
            rows_per_wgt = columns_per_act;
            columns_per_act = tmp;
        }
        auto windows_per_tile = N_COLUMNS/columns_per_act;
        auto filters_per_tile = N_ROWS/rows_per_wgt;

        auto num_filters_sets = (uint32_t)ceil(num_filters/(double)filters_per_tile);
        auto baseline_filters_sets = (uint32_t)ceil(num_filters/(double)N_ROWS);

        // Stats
        stats.cycles.emplace_back(std::vector<uint64_t>(batch_size,0));
        stats.idle_columns.push_back((uint64_t)(N_COLUMNS - windows_per_tile*columns_per_act));
        stats.idle_rows.push_back((uint64_t)(N_ROWS - filters_per_tile*rows_per_wgt));
        stats.columns_per_act.push_back((uint64_t)columns_per_act);
        stats.rows_per_wgt.push_back((uint64_t)rows_per_wgt);

        #ifndef FC_MULTIPLEX_COLUMNS

        // All FC in one column
        for (int n = 0; n<batch_size; n++) {
            auto precision_cycles = (columns_per_act == 1 && rows_per_wgt == 1) ? act_layer_prec : BITS_PE;
            for (int r = 0; r < R; r++) {
                for (int k = 0; k<act_channels; k += WEIGHT_LANES) {
                    stats.cycles.back()[n] += precision_cycles;
                }
            }
            stats.cycles.back()[n] *= num_filters_sets;
        }

        #else

        for (int n = 0; n<batch_size; n++) {

            int column_index = 0;
            std::vector<int> column_end = std::vector<int>(windows_per_tile, 0);
            auto precision_cycles = (columns_per_act == 1 && rows_per_wgt == 1) ? act_layer_prec : BITS_PE;

            for (int r = 0; r < R; r++) {
                for (int k = 0; k < act_channels; k += WEIGHT_LANES) {
                    if (stats.cycles.back()[n] < column_end[column_index])
                        stats.cycles.back()[n] = column_end[column_index];
                    auto column_cycles = precision_cycles;
                    column_end[column_index] = stats.cycles.back()[n] + column_cycles;
                    stats.cycles.back()[n]++;
                    column_index++;
                    if (column_index >= windows_per_tile) column_index = 0;
                }
            }
            uint64_t last_column_end = *std::max_element(column_end.begin(), column_end.end());
            uint64_t last_column_rem_cycles = last_column_end - stats.cycles.back()[n];
            stats.cycles.back()[n] *= num_filters_sets;
            stats.cycles.back()[n] += last_column_rem_cycles;
        }

        #endif

        auto base_cycles = (uint64_t)(ceil(act_channels/(double)WEIGHT_LANES) * baseline_filters_sets * R);

        std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1);

        stats.time.push_back(time_span);
        stats.baseline_cycles.push_back(base_cycles);

    }

    template <typename T>
    void Stripes<T>::run(const Network<T> &network) {
        // Initialize statistics
        sys::Statistics::Stats stats;
        sys::Statistics::initialize(stats);

        stats.task_name = "cycles";
        stats.net_name = network.getName();
        stats.arch = "Stripes_C" + std::to_string(N_COLUMNS) + "_R" + std::to_string(N_ROWS) + "_BP" +
                std::to_string(BITS_PE);

        for(const Layer<T> &layer : network.getLayers()) {
            if(layer.getType() == "Convolution") {
                stats.layers.push_back(layer.getName());
                stats.act_prec.push_back(layer.getAct_precision());
                stats.wgt_prec.push_back(layer.getWgt_precision());
                if(layer.getWeights().getShape()[1] == 1 && layer.getActivations().getShape()[1] != 1)
                    computeConvolution2D(layer, stats);
                else
                    computeConvolution(layer, stats);
            } else if(layer.getType() == "InnerProduct" || layer.getType() == "LSTM") {
                stats.layers.push_back(layer.getName());
                stats.act_prec.push_back(layer.getAct_precision());
                stats.wgt_prec.push_back(layer.getWgt_precision());
                computeInnerProduct(layer, stats);
            }
        }

        // Set statistics to write
        sys::Statistics::addStats(stats);

    }

    /* POTENTIALS */

    template <typename T>
    void Stripes<T>::computePotentialsConvolution(const core::Layer<T> &layer, sys::Statistics::Stats &stats) {

        std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();

        const cnpy::Array<T> &act = layer.getActivations();
        cnpy::Array<T> wgt = layer.getWeights();
        if(wgt.getDimensions() == 2) wgt.reshape_to_4D();

        const std::vector<size_t> &act_shape = act.getShape();
        const std::vector<size_t> &wgt_shape = wgt.getShape();

        int batch_size = 1;
        int Nx = act_shape[2];
        int Ny = act_shape[3];

        int num_filters = wgt_shape[0];
        int wgt_channels = wgt_shape[1];
        int Kx = wgt_shape[2];
        int Ky = wgt_shape[3];

        int padding = layer.getPadding();
        int stride = layer.getStride();

        long out_x = (Nx - Kx + 2*padding)/stride + 1;
        long out_y = (Ny - Ky + 2*padding)/stride + 1;

        // Operations
        const auto parallel_mult = (uint64_t)(num_filters * out_x * out_y * Kx * Ky * wgt_channels);
        stats.bit_multiplications.emplace_back(std::vector<uint64_t>(batch_size,0));
        stats.work_reduction.emplace_back(std::vector<double>(batch_size,0));
        stats.speedup.emplace_back(std::vector<double>(batch_size,0));
        uint64_t bit_counter = 0;

        // Get layer precision
        auto layer_prec = layer.getAct_precision();

        // Convolution
        for(int n=0; n<batch_size; n++) {
            bit_counter = (uint64_t)computeStripesBitsPE((uint8_t)layer_prec) * out_x * out_y * Kx * Ky * wgt_channels *
                    num_filters;
            stats.work_reduction.back()[n] = 100 - ((double)bit_counter / (double)parallel_mult / 256. * 100);
            stats.speedup.back()[n] = (double)parallel_mult * 256. / (double)bit_counter;
            stats.bit_multiplications.back()[n] = bit_counter;
        }

        std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1);

        stats.time.push_back(time_span);
        stats.parallel_multiplications.push_back(parallel_mult);

    }

    template <typename T>
    void Stripes<T>::computePotentialsInnerProduct(const Layer<T> &layer, sys::Statistics::Stats &stats) {

        std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();

        cnpy::Array<T> act = layer.getActivations();
        if(act.getDimensions() == 4) act.reshape_to_2D();
        const cnpy::Array<T> &wgt = layer.getWeights();

        const std::vector<size_t> &act_shape = act.getShape();
        const std::vector<size_t> &wgt_shape = wgt.getShape();

        int batch_size = 1;
        int R = (layer.getType() == "LSTM") ? act_shape[0] : 1;

        int num_filters = wgt_shape[0];
        int wgt_channels = wgt_shape[1];

        // Operations
        const auto parallel_mult = (uint64_t)num_filters * wgt_channels * R;
        stats.bit_multiplications.emplace_back(std::vector<uint64_t>(batch_size,0));
        stats.work_reduction.emplace_back(std::vector<double>(batch_size,0));
        stats.speedup.emplace_back(std::vector<double>(batch_size,0));
        uint64_t bit_counter = 0;

        // Get layer precision
        auto layer_prec = layer.getAct_precision();

        for (int n = 0; n<batch_size; n++) {
            bit_counter = (uint64_t)computeStripesBitsPE((uint8_t)layer_prec)*wgt_channels * num_filters * R;
            stats.work_reduction.back()[n] = 100 - ((double)bit_counter / (double)parallel_mult / 256. * 100);
            stats.speedup.back()[n] = (double)parallel_mult * 256. / (double)bit_counter;
            stats.bit_multiplications.back()[n] = bit_counter;
        }

        std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1);

        stats.time.push_back(time_span);
        stats.parallel_multiplications.push_back(parallel_mult);

    }

    template <typename T>
    void Stripes<T>::potentials(const Network<T> &network) {
        // Initialize statistics
        sys::Statistics::Stats stats;
        sys::Statistics::initialize(stats);

        stats.task_name = "potentials";
        stats.net_name = network.getName();
        stats.arch = "Stripes";

        for(const Layer<T> &layer : network.getLayers()) {
            if(layer.getType() == "Convolution") {
                stats.layers.push_back(layer.getName());
                stats.act_prec.push_back(layer.getAct_precision());
                stats.wgt_prec.push_back(0);
                computePotentialsConvolution(layer,stats);
            } else if (layer.getType() == "InnerProduct" || layer.getType() == "LSTM") {
                stats.layers.push_back(layer.getName());
                stats.act_prec.push_back(layer.getAct_precision());
                stats.wgt_prec.push_back(0);
                computePotentialsInnerProduct(layer,stats);
            }
        }

        // Set statistics to write
        sys::Statistics::addStats(stats);
    }

    template class Stripes<uint16_t>;

}

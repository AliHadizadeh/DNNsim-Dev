#ifndef DNNSIM_STATISTICS_H
#define DNNSIM_STATISTICS_H

#include <sys/common.h>

namespace sys {

    class Statistics {

    public:

        /* Make public to be easier to operate */
        struct Stats {

            /* Name of the task done */
            std::string task_name;

            /* Name of the network */
            std::string net_name;

            /* Simulator architecture */
            std::string arch;

            /* Tensforflow flag */
            bool tensorflow_8b;

            /* Layer vector */
            std::vector<std::string> layers;

            /* Activations precision */
            std::vector<int> act_prec;

            /* Weights precision */
            std::vector<int> wgt_prec;

            /* Computation time per layer */
            std::vector<std::chrono::duration<double>> time;

            /* Stats for cycles */
            std::vector<std::vector<uint64_t>> cycles;
            std::vector<uint64_t> baseline_cycles;

            /* Stats for column stalls */
            std::vector<std::vector<uint64_t>> stall_cycles;

            /* Stats for 8bits PEs */
            std::vector<uint64_t> idle_columns;
            std::vector<uint64_t> idle_rows;
            std::vector<uint64_t> columns_per_act;
            std::vector<uint64_t> rows_per_wgt;

            /* SCNN */
            std::vector<std::vector<uint64_t>> dense_cycles;
            std::vector<std::vector<uint64_t>> mults;
            std::vector<std::vector<uint64_t>> idle_bricks;
            std::vector<std::vector<uint64_t>> idle_conflicts;
            std::vector<std::vector<uint64_t>> idle_column_cycles;
            std::vector<std::vector<uint64_t>> column_stalls;
            std::vector<std::vector<uint64_t>> idle_pe;
            std::vector<std::vector<uint64_t>> idle_halo;
            std::vector<std::vector<uint64_t>> total_mult_cycles;
            std::vector<std::vector<uint64_t>> halo_transfers;
            std::vector<std::vector<uint64_t>> weight_buff_reads;
            std::vector<std::vector<uint64_t>> act_buff_reads;
            std::vector<std::vector<uint64_t>> accumulator_updates;
            std::vector<std::vector<uint64_t>> i_loop;
            std::vector<std::vector<uint64_t>> f_loop;
            std::vector<std::vector<uint64_t>> offchip_weight_reads;

            /* Bit Fusion */
            std::vector<uint64_t> perf_factor;
            std::vector<uint64_t> time_multiplex;

            /* Stats for potentials */
            std::vector<std::vector<double>> work_reduction;
            std::vector<std::vector<double>> speedup;
            std::vector<std::vector<uint64_t>> bit_multiplications;
            std::vector<uint64_t> parallel_multiplications;

            /* Stats for sparsity */
            std::vector<double> act_sparsity;
            std::vector<uint64_t> zero_act;
            std::vector<uint64_t> total_act;
            std::vector<double> wgt_sparsity;
            std::vector<uint64_t> zero_wgt;
            std::vector<uint64_t> total_wgt;

			/* Stats for training sparsity */
            std::vector<std::vector<double>> fw_act_sparsity;
            std::vector<std::vector<uint64_t>> fw_zero_act;
            std::vector<std::vector<uint64_t>> fw_total_act;
            std::vector<std::vector<double>> fw_wgt_sparsity;
            std::vector<std::vector<uint64_t>> fw_zero_wgt;
            std::vector<std::vector<uint64_t>> fw_total_wgt;
            std::vector<std::vector<double>> fw_bias_sparsity;
            std::vector<std::vector<uint64_t>> fw_zero_bias;
            std::vector<std::vector<uint64_t>> fw_total_bias;

            std::vector<std::vector<double>> bw_act_grad_sparsity;
            std::vector<std::vector<uint64_t>> bw_zero_act_grad;
            std::vector<std::vector<uint64_t>> bw_total_act_grad;
            std::vector<std::vector<double>> bw_wgt_grad_sparsity;
            std::vector<std::vector<uint64_t>> bw_zero_wgt_grad;
            std::vector<std::vector<uint64_t>> bw_total_wgt_grad;
            std::vector<std::vector<double>> bw_bias_grad_sparsity;
            std::vector<std::vector<uint64_t>> bw_zero_bias_grad;
            std::vector<std::vector<uint64_t>> bw_total_bias_grad;
            std::vector<std::vector<double>> bw_out_act_grad_sparsity;
            std::vector<std::vector<uint64_t>> bw_zero_out_act_grad;
            std::vector<std::vector<uint64_t>> bw_total_out_act_grad;

            /* Stats for average width */
            std::vector<std::vector<double>> act_avg_width;
            std::vector<std::vector<double>> act_width_reduction;
            std::vector<std::vector<std::vector<double>>> act_width_need;
            std::vector<std::vector<uint64_t>> act_bits_baseline;
            std::vector<std::vector<uint64_t>> act_bits_profiled;
            std::vector<std::vector<uint64_t>> act_bits_datawidth;
            std::vector<std::vector<uint64_t>> act_bits_scnn;
            std::vector<std::vector<double>> wgt_avg_width;
            std::vector<std::vector<double>> wgt_width_reduction;
            std::vector<std::vector<std::vector<double>>> wgt_width_need;
            std::vector<std::vector<uint64_t>> wgt_bits_baseline;
            std::vector<std::vector<uint64_t>> wgt_bits_profiled;
            std::vector<std::vector<uint64_t>> wgt_bits_datawidth;
            std::vector<std::vector<uint64_t>> wgt_bits_scnn;

            template <typename T>
            T get_average(const std::vector<T> &vector_stat) const {
                return accumulate(vector_stat.begin(), vector_stat.end(), 0.0) / vector_stat.size();
            }

            template <typename T>
            T get_average(const std::vector<std::vector<T>> &vector_stat) const {
                std::vector<T> averages = std::vector<T>(vector_stat.size(),0);
                for(int i = 0; i < vector_stat.size(); i++) {
                    averages[i] = this->get_average(vector_stat[i]);
                }
                return this->get_average(averages);
            }

            template <typename T>
            T get_total(const std::vector<T> &vector_stat) const {
                return accumulate(vector_stat.begin(), vector_stat.end(), 0.0);
            }

            template <typename T>
            T get_total(const std::vector<std::vector<T>> &vector_stat) const {
                std::vector<T> averages = std::vector<T>(vector_stat.size(),0);
                for(int i = 0; i < vector_stat.size(); i++) {
                    averages[i] = this->get_average(vector_stat[i]);
                }
                return this->get_total(averages);
            }

        };

    private:

        /* Set of statistics containing all simulation stats. Shared across all system */
        static std::vector<Stats> all_stats;

    public:

        /* Initializes values of the struct
         * @param stats Stats struct we want to initialize
         */
        static void initialize(Stats &stats);

        /* Add one stats struct to the set of statistics
         * @param _stats    Stats struct that is going to be added
         */
        static void addStats(const Stats &_stats);

        /* Update flags for the last stats
         * @param TENSORFLOW_8b     Tensorflow 8b network
         */
        static void updateFlagsLastStat(bool TENSORFLOW_8b);

        /* Getter */
        static const std::vector<Stats> &getAll_stats();

    };

}

#endif //DNNSIM_STATISTICS_H

/*
Copyright (c) 2018-Present Isak Edo Vivancos, Andreas Moshovos
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <sys/common.h>
#include <sys/cxxopts.h>
#include <sys/Batch.h>

#include <interface/NetReader.h>
#include <interface/NetWriter.h>

#include <base/Network.h>
#include <core/Inference.h>
#include <core/Parallel.h>
#include <core/Stripes.h>
#include <core/ShapeShifter.h>
#include <core/Loom.h>
#include <core/BitPragmatic.h>
#include <core/Laconic.h>
#include <core/TensorDash.h>
#include <core/SCNN.h>
#include <core/BitTactical.h>
#include <core/WindowFirstOutS.h>

template <typename T>
base::Network<T> read(const sys::Batch::Simulate &simulate, bool QUIET) {

    // Read the network
    base::Network<T> network;
    interface::NetReader<T> reader = interface::NetReader<T>(simulate.network, simulate.batch, 0, QUIET);
    if (simulate.model == "Caffe") {
        network = reader.read_network_caffe();
        network.setNetwork_bits(simulate.network_bits);
        network.setTensorflow_8b(simulate.tensorflow_8b);
        network.setIntelINQ(simulate.intel_inq);
        reader.read_precision(network);
        reader.read_weights_npy(network);
        reader.read_activations_npy(network);

    } else if (simulate.model == "Trace") {
        network = reader.read_network_trace_params();
        network.setNetwork_bits(simulate.network_bits);
        network.setTensorflow_8b(simulate.tensorflow_8b);
        network.setIntelINQ(simulate.intel_inq);
        reader.read_precision(network);
        reader.read_weights_npy(network);
        reader.read_activations_npy(network);

    } else if (simulate.model == "CParams") {
        network = reader.read_network_conv_params();
        network.setNetwork_bits(simulate.network_bits);
        network.setTensorflow_8b(simulate.tensorflow_8b);
        network.setIntelINQ(simulate.intel_inq);
        reader.read_weights_npy(network);
        reader.read_activations_npy(network);

    } else if (simulate.model == "Protobuf") {
        network = reader.read_network_protobuf();
    } else {
		throw std::runtime_error("Input model option not recognized");
	}

    return network;

}

template <typename T>
void write(const base::Network<T> &network, bool QUIET) {

    // Write network
    interface::NetWriter<T> writer = interface::NetWriter<T>(network.getName(),QUIET);
    writer.write_network_protobuf(network);

}

void check_options(const cxxopts::Options &options)
{
    if(options.count("batch") == 0) {
        throw std::runtime_error("Please provide a batch file with instructions. Examples in folder \"examples\"");
    } else {
        std::string batch_path = options["batch"].as<std::string>();
        std::ifstream file(batch_path);
        if(!file.good()) {
            throw std::runtime_error("The path " + batch_path + " does not exist.");
        }
    }

    if(options.count("threads") == 1 && options["threads"].as<uint16_t>() < 1) {
        throw std::runtime_error("The number of threads must be at least one.");
    }

}

cxxopts::Options parse_options(int argc, char *argv[]) {
    cxxopts::Options options("DNNsim", "Deep Neural Network simulator");

    // help-related options
    options.add_options("help")("h,help", "Print this help message", cxxopts::value<bool>(), "");

    options.add_options("batch")
    ("batch", "Specify a batch file with instructions. Examples in folder \"examples\"",cxxopts::value<std::string>(),
            "<Prototxt file>");

    options.add_options("simulation")
    ("t,threads", "Specify the number of threads",cxxopts::value<uint16_t>(),"<Positive number>")
    ("q,quiet", "Don't show stdout progress messages",cxxopts::value<bool>(),"<Boolean>")
    ("fast_mode", "Enable fast mode: simulate only one image",cxxopts::value<bool>(),"<Boolean>")
    ("store_fixed_point_protobuf", "Store the fixed point network in an intermediate Protobuf file",
    		cxxopts::value<bool>(),"<Boolean>")
    ("check_values", "Check the correctness of the output values of the simulations.", cxxopts::value<bool>(),
            "<Boolean>");

    options.parse_positional("batch");

    options.parse(argc, argv);

    return options;
}

int main(int argc, char *argv[]) {
    try {
        auto const options = parse_options(argc, argv);

        // Help
        if (options.count("h") == 1) {
            std::cout << options.help({"help", "batch", "simulation"}) << std::endl;
            return 0;
        }

        check_options(options);

        uint8_t N_THREADS = options.count("threads") == 0 ? (uint8_t)1 : (uint8_t)options["threads"].as<uint16_t>();
        bool QUIET = options.count("quiet") == 0 ? false : options["quiet"].as<bool>();
        bool FAST_MODE = options.count("fast_mode") == 0 ? false : options["fast_mode"].as<bool>();
        bool STORE_PROTOBUF = options.count("store_fixed_point_protobuf") == 0 ? false :
        		options["store_fixed_point_protobuf"].as<bool>();
        bool CHECK = options.count("check_values") == 0 ? false : options["check_values"].as<bool>();
        std::string batch_path = options["batch"].as<std::string>();
        sys::Batch batch = sys::Batch(batch_path);
        batch.read_batch();

        for(const auto &simulate : batch.getSimulations()) {

            if(!QUIET) std::cout << "Network: " << simulate.network << std::endl;

            try {
				// Training traces
				if(simulate.training) {

                    uint32_t epochs = simulate.epochs;
					for(const auto &experiment : simulate.experiments) {

                        if (experiment.architecture == "TensorDash") {
                            core::TensorDash<float> DNNsim(experiment.n_lanes, experiment.n_columns,
                                    experiment.n_rows, experiment.n_tiles, experiment.lookahead_h,
                                    experiment.lookaside_d, experiment.search_shape.c_str()[0], experiment.banks,
                                    N_THREADS, FAST_MODE, QUIET, CHECK);
                            if (experiment.task == "Cycles") DNNsim.run(simulate, epochs);
                            else if (experiment.task == "Potentials") DNNsim.potentials(simulate, epochs);
                        }

		            }

				// Inference traces
				} else {

		            if (simulate.data_type == "Float32") {
		                auto network = read<float>(simulate, QUIET);
		                for(const auto &experiment : simulate.experiments) {

                            if(!QUIET) std::cout << "Starting simulation " << experiment.task << " for architecture "
                                                 << experiment.architecture << std::endl;

                            core::BitTactical<float> scheduler(experiment.n_lanes, experiment.n_rows,
                                    experiment.lookahead_h, experiment.lookaside_d, experiment.search_shape.c_str()[0]);

                            std::shared_ptr<core::Dataflow<float>> dataflow =
                                    std::make_shared<core::WindowFirstOutS<float>>(scheduler);

                            core::Inference<float> DNNsim(experiment.n_lanes, experiment.n_columns,
                                    experiment.n_rows, experiment.n_tiles, experiment.bits_pe, N_THREADS, FAST_MODE,
                                    QUIET, CHECK);

		                    if (experiment.architecture == "SCNN") {
                                std::shared_ptr<core::Architecture<float>> arch =
                                        std::make_shared<core::SCNN<float>>(experiment.Wt, experiment.Ht, experiment.I,
                                        experiment.F, experiment.out_acc_size, experiment.banks, N_THREADS, FAST_MODE,
                                        QUIET);

		                        if (experiment.task == "Cycles")
		                            std::static_pointer_cast<core::SCNN<float>>(arch)->run(network);
		                        else if (experiment.task == "Potentials") DNNsim.potentials(network, arch);

		                    } else if (experiment.architecture == "Parallel") {
                                std::shared_ptr<core::Architecture<float>> arch =
                                        std::make_shared<core::Parallel<float>>(experiment.tactical);

                                if (experiment.task == "Cycles") DNNsim.run(network, arch, dataflow);
                                else if (experiment.task == "Potentials") DNNsim.potentials(network, arch);
		                    }
		                }

		            } else if (simulate.data_type == "Fixed16") {
                        base::Network<uint16_t> network;
                        if (simulate.model != "Protobuf") {
                            base::Network<float> tmp_network;
                            tmp_network = read<float>(simulate, QUIET);
                            network = tmp_network.fixed_point();
                        } else {
                            network = read<uint16_t>(simulate, QUIET);
						}

						if(STORE_PROTOBUF) write(network, QUIET);

						for(const auto &experiment : simulate.experiments) {

                            if(!QUIET) std::cout << "Starting simulation " << experiment.task << " for architecture "
                                    << experiment.architecture << std::endl;

                            core::BitTactical<uint16_t> scheduler(experiment.n_lanes, experiment.n_rows,
                                    experiment.lookahead_h, experiment.lookaside_d, experiment.search_shape.c_str()[0]);

                            std::shared_ptr<core::Dataflow<uint16_t>> dataflow =
                                    std::make_shared<core::WindowFirstOutS<uint16_t>>(scheduler);

                            core::Inference<uint16_t> DNNsim(experiment.n_lanes, experiment.n_columns,
                                    experiment.n_rows, experiment.n_tiles, experiment.bits_pe, N_THREADS, FAST_MODE,
                                    QUIET, CHECK);

                            if (experiment.architecture == "SCNN") {
                                std::shared_ptr<core::Architecture<uint16_t>> arch =
                                        std::make_shared<core::SCNN<uint16_t>>(experiment.Wt, experiment.Ht,
                                        experiment.I, experiment.F, experiment.out_acc_size, experiment.banks,
                                        N_THREADS, FAST_MODE, QUIET);

                                if (experiment.task == "Cycles")
                                    std::static_pointer_cast<core::SCNN<uint16_t>>(arch)->run(network);
                                else if (experiment.task == "Potentials") DNNsim.potentials(network, arch);

                            } else if (experiment.architecture == "Parallel") {
                                std::shared_ptr<core::Architecture<uint16_t>> arch =
                                        std::make_shared<core::Parallel<uint16_t>>(experiment.tactical);

                                if (experiment.task == "Cycles") DNNsim.run(network, arch, dataflow);
                                else if (experiment.task == "Potentials") DNNsim.potentials(network, arch);

                            } else if (experiment.architecture == "Stripes") {
                                std::shared_ptr<core::Architecture<uint16_t>> arch =
                                        std::make_shared<core::Stripes<uint16_t>>();

                                if(experiment.task == "Cycles") DNNsim.run(network, arch, dataflow);
                                else if (experiment.task == "Potentials") DNNsim.potentials(network, arch);

                            } else if (experiment.architecture == "ShapeShifter") {
                                std::shared_ptr<core::Architecture<uint16_t>> arch =
                                        std::make_shared<core::ShapeShifter<uint16_t>>(experiment.precision_granularity,
                                        experiment.column_registers, experiment.minor_bit, experiment.diffy,
                                        experiment.tactical);

                                if(experiment.task == "Cycles") DNNsim.run(network, arch, dataflow);
                                else if (experiment.task == "Potentials") DNNsim.potentials(network, arch);

                            } else if (experiment.architecture == "Loom") {
                                std::shared_ptr<core::Architecture<uint16_t>> arch =
                                        std::make_shared<core::Loom<uint16_t>>(experiment.precision_granularity,
                                        experiment.pe_serial_bits, experiment.minor_bit, experiment.dynamic_weights);

                                if(experiment.task == "Cycles") DNNsim.run(network, arch, dataflow);
                                else if (experiment.task == "Potentials") DNNsim.potentials(network, arch);

                            } else if (experiment.architecture == "BitPragmatic") {
                                std::shared_ptr<core::Architecture<uint16_t>> arch =
                                        std::make_shared<core::BitPragmatic<uint16_t>>(experiment.bits_first_stage,
                                        experiment.column_registers, experiment.booth, experiment.diffy,
                                        experiment.tactical);

                                if(experiment.task == "Cycles") DNNsim.run(network, arch, dataflow);
                                else if (experiment.task == "Potentials") DNNsim.potentials(network, arch);

                            } else if (experiment.architecture == "Laconic") {
                                std::shared_ptr<core::Architecture<uint16_t>> arch =
                                        std::make_shared<core::Laconic<uint16_t>>(experiment.booth);

                                if(experiment.task == "Cycles") DNNsim.run(network, arch, dataflow);
                                else if (experiment.task == "Potentials") DNNsim.potentials(network, arch);

                            }

		                }
		            }
				}
            
			} catch (std::exception &exception) {
                std::cerr << "Simulation error: " << exception.what() << std::endl;
                #ifdef STOP_AFTER_ERROR
                exit(1);
                #endif
            }
        }

    } catch (std::exception &exception) {
        std::cerr << "Error: " << exception.what() << std::endl;
        exit(1);
    }
    return 0;
}

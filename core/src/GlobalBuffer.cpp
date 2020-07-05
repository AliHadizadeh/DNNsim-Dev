
#include <core/GlobalBuffer.h>
#include <cstdint>

namespace core {

    template <typename T>
    uint64_t GlobalBuffer<T>::getActSize() const {
        return ACT_SIZE.front();
    }

    template <typename T>
    uint64_t GlobalBuffer<T>::getWgtSize() const {
        return WGT_SIZE.front();
    }

    template <typename T>
    uint32_t GlobalBuffer<T>::getActBanks() const {
        return ACT_BANKS;
    }

    template <typename T>
    uint32_t GlobalBuffer<T>::getWgtBanks() const {
        return WGT_BANKS;
    }

    template<typename T>
    uint32_t GlobalBuffer<T>::getOutBanks() const {
        return OUT_BANKS;
    }

    template<typename T>
    uint32_t GlobalBuffer<T>::getActAddrsPerAccess() const {
        return ACT_ADDRS_PER_ACCESS;
    }

    template<typename T>
    uint64_t GlobalBuffer<T>::getActReads() const {
        return act_reads;
    }

    template<typename T>
    uint64_t GlobalBuffer<T>::getPsumReads() const {
        return psum_reads;
    }

    template<typename T>
    uint64_t GlobalBuffer<T>::getWgtReads() const {
        return wgt_reads;
    }

    template<typename T>
    uint64_t GlobalBuffer<T>::getOutWrites() const {
        return out_writes;
    }

    template<typename T>
    uint64_t GlobalBuffer<T>::getActBankConflicts() const {
        return act_bank_conflicts;
    }

    template<typename T>
    uint64_t GlobalBuffer<T>::getPsumBankConflicts() const {
        return psum_bank_conflicts;
    }

    template<typename T>
    uint64_t GlobalBuffer<T>::getWgtBankConflicts() const {
        return wgt_bank_conflicts;
    }

    template<typename T>
    uint64_t GlobalBuffer<T>::getOutBankConflicts() const {
        return out_bank_conflicts;
    }

    template <typename T>
    bool GlobalBuffer<T>::data_ready() const {
        return read_ready_cycle <= *this->global_cycle;
    }

    template <typename T>
    std::string GlobalBuffer<T>::filename() {
        return "_AM" + to_mem_string(ACT_SIZE.front()) + "_WM" + to_mem_string(WGT_SIZE.front());
    }

    template <typename T>
    std::string GlobalBuffer<T>::header() {
        std::string header = "Activations memory size: ";
        for (const auto &size : ACT_SIZE) header += to_mem_string(size) + " "; header += "\n";
        header += "Weight memory size: ";
        for (const auto &size : WGT_SIZE) header += to_mem_string(size) + " "; header += "\n";
        header += "Number of activation banks: " + std::to_string(ACT_BANKS) + "\n";
        header += "Number of weight banks: " + std::to_string(WGT_BANKS) + "\n";
        header += "Number of output banks: " + std::to_string(OUT_BANKS) + "\n";
        header += "Activation bank interface width: " + std::to_string(ACT_BANK_WIDTH) + "\n";
        header += "Weight bank interface width: " + std::to_string(WGT_BANK_WIDTH) + "\n";
        header += "Activations read delay: ";
        for (const auto &delay : ACT_READ_DELAY) header += std::to_string(delay) + " "; header += "\n";
        header += "Activations write delay: ";
        for (const auto &delay : ACT_WRITE_DELAY) header += std::to_string(delay) + " "; header += "\n";
        header += "Weights read delay: ";
        for (const auto &delay : WGT_READ_DELAY) header += std::to_string(delay) + " "; header += "\n";
        return header;
    }

    template <typename T>
    void GlobalBuffer<T>::configure_layer() {
        psum_read_ready_cycle = 0;
        read_ready_cycle = 0;
        write_ready_cycle = 0;

        act_reads = 0;
        psum_reads = 0;
        wgt_reads = 0;
        out_writes = 0;

        act_bank_conflicts = 0;
        psum_bank_conflicts = 0;
        wgt_bank_conflicts = 0;
        out_bank_conflicts = 0;

        for (int lvl = 1; lvl < ACT_LEVELS; ++lvl) {
            for (int bank = 0; bank < ACT_BANKS; ++bank) {
                act_eviction_policy[lvl][bank]->flush();
            }
            for (int bank = 0; bank < OUT_BANKS; ++bank) {
                out_eviction_policy[lvl][bank]->flush();
            }
        }

        for (int lvl = 1; lvl < WGT_LEVELS; ++lvl) {
            for (int bank = 0; bank < WGT_BANKS; ++bank) {
                wgt_eviction_policy[lvl][bank]->flush();
            }
        }
    }

    template<typename T>
    bool GlobalBuffer<T>::write_done() {
        return write_ready_cycle <= *this->global_cycle;
    }

    template <typename T>
    void GlobalBuffer<T>::act_read_request(const std::shared_ptr<TilesData<T>> &tiles_data, bool layer_act_on_chip,
            bool &read_act) {

        try {

            auto bank_addr_reads = std::vector<std::vector<int>>(ACT_LEVELS, std::vector<int>(ACT_BANKS, 0));

            for (const auto &tile_data : tiles_data->data) {

                if (!tile_data.valid || tile_data.act_addresses.empty())
                    continue;

                assert(tile_data.act_banks.size() == tile_data.act_addresses.size());
                assert(tile_data.act_banks.front().size() == tile_data.act_addresses.front().size());

                read_act = true;
                uint64_t rows = tile_data.act_banks.size();
                uint64_t n_addr = tile_data.act_banks.front().size();
                for (int row = 0; row < rows; ++row) {
                    for (int idx = 0; idx < n_addr; ++idx) {

                        const auto &act_addr = tile_data.act_addresses[row][idx];
                        if (act_addr == NULL_ADDR)
                            continue;

                        if (layer_act_on_chip) {
                            auto it = (*this->tracked_data).find(act_addr);
                            if (it == (*this->tracked_data).end())
                                this->tracked_data->insert({act_addr,  1});
                        }

                        const auto &act_lvl = (*this->tracked_data).at(act_addr);
                        const auto &act_bank = tile_data.act_banks[row][idx];

                        assert(act_bank != -1);
                        assert(act_lvl >= 0 && act_lvl <= ACT_LEVELS);

                        bank_addr_reads[ACT_LEVELS - 1][act_bank]++;
                        if (ACT_LEVELS > 1) {
                            for (int lvl = ACT_LEVELS - 1; lvl >= act_lvl; --lvl) {
                                bank_addr_reads[lvl - 1][act_bank]++;
                                if (!act_eviction_policy[lvl][act_bank]->free_entry()) {
                                    auto evict_addr = act_eviction_policy[lvl][act_bank]->evict_addr();
                                    assert((*this->tracked_data).at(evict_addr) == lvl + 1);
                                    (*this->tracked_data).at(act_addr) = lvl;
                                }
                                act_eviction_policy[lvl][act_bank]->insert_addr(act_addr);
                            }
                        }
                        if (act_lvl > 1) {
                            for (int lvl = act_lvl - 1; lvl >= 1; --lvl) {
                                act_eviction_policy[lvl][act_bank]->update_policy(act_addr);
                            }
                        }

                        (*this->tracked_data).at(act_addr) = ACT_LEVELS;

                    }
                }

            }

            uint64_t start_time = read_act ? *this->global_cycle : 0;
            for (int lvl = 0; lvl < ACT_LEVELS; ++lvl) {

                auto bank_steps = 0;
                for (const auto &reads : bank_addr_reads[lvl]) {
                    auto bank_reads = ceil(reads / (double)ACT_ADDRS_PER_ACCESS);
                    act_reads += bank_reads;
                    if (bank_reads > bank_steps)
                        bank_steps = bank_reads;
                }

                start_time += bank_steps * ACT_READ_DELAY[lvl];
                if (lvl == ACT_LEVELS - 1) act_bank_conflicts += bank_steps > 0 ? bank_steps - 1 : 0;

            }

            read_ready_cycle = std::max(read_ready_cycle, start_time);

        } catch (std::exception &exception) {
            throw std::runtime_error("Global Buffer waiting for a memory address not requested.");
        }

    }

    template <typename T>
    void GlobalBuffer<T>::psum_read_request(const std::shared_ptr<TilesData<T>> &tiles_data, bool &read_psum) {

        try {

            // TODO
            bool first = true;
            uint64_t start_time = 0;
            auto bank_addr_reads = std::vector<std::vector<int>>(ACT_LEVELS, std::vector<int>(OUT_BANKS, 0));

            for (const auto &tile_data : tiles_data->data) {

                if (!tile_data.valid || tile_data.psum_addresses.empty())
                    continue;

                    if (first) {
                        start_time = std::max(*this->global_cycle, write_ready_cycle);
                        first = false;
                    }

                    assert(tile_data.out_banks.size() == tile_data.out_addresses.size());
                    uint64_t n_addr = tile_data.out_banks.size();

                    for (int idx = 0; idx < n_addr; ++idx) {

                        const auto &out_addr = tile_data.out_addresses[idx];
                        if (out_addr == NULL_ADDR)
                            continue;

                        const auto &out_lvl = (*this->tracked_data).at(out_addr) - 1;
                        const auto &out_bank = tile_data.out_banks[idx];

                        assert(out_lvl < ACT_LEVELS);
                        assert(out_bank != -1);

                        for (int lvl = out_lvl; lvl < ACT_LEVELS; ++lvl) {
                            bank_addr_reads[lvl][out_bank]++;
                        }

                        (*this->tracked_data).at(out_addr) = ACT_LEVELS;

                    }

            }

            for (int lvl = 0; lvl < ACT_LEVELS; ++lvl) {

                auto bank_steps = 0;
                for (const auto &reads : bank_addr_reads[lvl]) {
                    auto bank_reads = ceil(reads /(double)ACT_ADDRS_PER_ACCESS);
                    psum_reads += bank_reads;
                    if (bank_reads > bank_steps)
                        bank_steps = bank_reads;
                }

                start_time += bank_steps * ACT_READ_DELAY[lvl];
                if (lvl ==  ACT_LEVELS - 1) psum_bank_conflicts += bank_steps > 0 ? bank_steps - 1 : 0;

            }

            psum_read_ready_cycle = start_time;
            read_ready_cycle = std::max(read_ready_cycle, psum_read_ready_cycle);
            read_psum = !first;

        } catch (std::exception &exception) {
            throw std::runtime_error("Global Buffer waiting for a memory address not requested.");
        }

    }

    template <typename T>
    void GlobalBuffer<T>::wgt_read_request(const std::shared_ptr<TilesData<T>> &tiles_data, bool &read_wgt) {

        try {

            auto bank_addr_reads = std::vector<std::vector<int>>(WGT_LEVELS, std::vector<int>(WGT_BANKS, 0));

            for (const auto &tile_data : tiles_data->data) {

                if (!tile_data.valid || tile_data.wgt_addresses.empty())
                    continue;

                assert(tile_data.wgt_banks.size() == tile_data.wgt_addresses.size());

                read_wgt = true;
                uint64_t n_addr = tile_data.wgt_banks.size();
                for (int idx = 0; idx < n_addr; ++idx) {

                    const auto &wgt_addr = tile_data.wgt_addresses[idx];
                    if (wgt_addr == NULL_ADDR)
                        continue;

                    const auto &wgt_lvl = (*this->tracked_data).at(wgt_addr);
                    const auto &wgt_bank = tile_data.wgt_banks[idx];

                    assert(wgt_bank != -1);
                    assert(wgt_lvl >= 0 && wgt_lvl <= WGT_LEVELS);

                    bank_addr_reads[WGT_LEVELS - 1][wgt_bank]++;
                    if (WGT_LEVELS > 1) {
                        for (int lvl = WGT_LEVELS - 1; lvl >= wgt_lvl; --lvl) {
                            bank_addr_reads[lvl - 1][wgt_bank]++;
                            if (!wgt_eviction_policy[lvl][wgt_bank]->free_entry()) {
                                auto evict_addr = wgt_eviction_policy[lvl][wgt_bank]->evict_addr();
                                assert((*this->tracked_data).at(evict_addr) == lvl + 1);
                                (*this->tracked_data).at(wgt_addr) = lvl;
                            }
                            wgt_eviction_policy[lvl][wgt_bank]->insert_addr(wgt_addr);
                        }
                    }
                    if (wgt_lvl > 1) {
                        for (int lvl = wgt_lvl - 1; lvl >= 1; --lvl) {
                            wgt_eviction_policy[lvl][wgt_bank]->update_policy(wgt_addr);
                        }
                    }

                    (*this->tracked_data).at(wgt_addr) = WGT_LEVELS;


                }

            }

            uint64_t start_time = read_wgt ? *this->global_cycle : 0;
            for (int lvl = 0; lvl < WGT_LEVELS; ++lvl) {

                auto bank_steps = 0;
                for (const auto &reads : bank_addr_reads[lvl]) {
                    auto bank_reads = ceil(reads / (double)WGT_ADDRS_PER_ACCESS);
                    wgt_reads += bank_reads;
                    if (bank_reads > bank_steps)
                        bank_steps = bank_reads;
                }

                start_time += bank_steps * WGT_READ_DELAY[lvl];
                if (lvl ==  WGT_LEVELS - 1) wgt_bank_conflicts += bank_steps > 0 ? bank_steps - 1 : 0;

            }

            read_ready_cycle = std::max(read_ready_cycle, start_time);

        } catch (std::exception &exception) {
            throw std::runtime_error("Global Buffer waiting for a memory address not requested.");
        }

    }

    template <typename T>
    void GlobalBuffer<T>::write_request(const std::shared_ptr<TilesData<T>> &tiles_data) {

        // TODO
        auto start_time = std::max(psum_read_ready_cycle, write_ready_cycle);
        start_time = std::max(start_time, *this->global_cycle);

        auto bank_conflicts = std::vector<int>(OUT_BANKS, 0);
        for (const auto &tile_data : tiles_data->data) {

            if (!tile_data.valid || tile_data.out_addresses.empty())
                continue;

            // Bank conflicts
            for (const auto &out_bank : tile_data.out_banks)
                if (out_bank != -1)
                    bank_conflicts[out_bank]++;

        }

        auto bank_steps = 0;
        for (const auto &writes : bank_conflicts) {
            auto bank_writes = ceil(writes /(double)ACT_ADDRS_PER_ACCESS);
            out_writes += bank_writes;
            if (bank_writes > bank_steps)
                bank_steps = bank_writes;
        }

        write_ready_cycle = start_time + bank_steps * ACT_WRITE_DELAY.back();
        out_bank_conflicts += bank_steps > 0 ? bank_steps - 1 : 0;

    }

    template <typename T>
    void GlobalBuffer<T>::evict_data(bool evict_act, bool evict_out, bool evict_wgt) {
        if (evict_act) {

            auto min_addr = std::get<0>(*this->act_addresses);
            auto max_addr = std::get<1>(*this->act_addresses);

            if (min_addr != NULL_ADDR) {
                auto it = this->tracked_data->find(min_addr);
                auto it2 = this->tracked_data->find(max_addr);
                this->tracked_data->erase(it, it2);
                this->tracked_data->erase(max_addr);
                *this->act_addresses = {NULL_ADDR, 0};
            }

            for (int lvl = 1; lvl < ACT_LEVELS; ++lvl) {
                for (int bank = 0; bank < ACT_BANKS; ++bank) {
                    act_eviction_policy[lvl][bank]->flush();
                }
            }

        }

        if (evict_out) {

            auto min_addr = std::get<0>(*this->out_addresses);
            auto max_addr = std::get<1>(*this->out_addresses);

            if (min_addr != NULL_ADDR) {
                auto it = this->tracked_data->find(min_addr);
                auto it2 = this->tracked_data->find(max_addr);
                this->tracked_data->erase(it, it2);
                this->tracked_data->erase(max_addr);
                *this->out_addresses = {NULL_ADDR, 0};
            }

            for (int lvl = 1; lvl < ACT_LEVELS; ++lvl) {
                for (int bank = 0; bank < OUT_BANKS; ++bank) {
                    out_eviction_policy[lvl][bank]->flush();
                }
            }

        }

        if (evict_wgt) {

            auto min_addr = std::get<0>(*this->wgt_addresses);
            auto max_addr = std::get<1>(*this->wgt_addresses);

            if (min_addr != NULL_ADDR) {
                auto it = this->tracked_data->find(min_addr);
                auto it2 = this->tracked_data->find(max_addr);
                this->tracked_data->erase(it, it2);
                this->tracked_data->erase(max_addr);
                *this->wgt_addresses = {NULL_ADDR, 0};
            }

            for (int lvl = 1; lvl < WGT_LEVELS; ++lvl) {
                for (int bank = 0; bank < WGT_BANKS; ++bank) {
                    wgt_eviction_policy[lvl][bank]->flush();
                }
            }

        }
    }

    INITIALISE_DATA_TYPES(GlobalBuffer);

}

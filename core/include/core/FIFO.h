#ifndef DNNSIM_FIFO_H
#define DNNSIM_FIFO_H

#include "EvictionPolicy.h"

namespace core {

    class FIFO : public EvictionPolicy {

    private:

        std::queue<uint64_t> fifo;

        void flush() override;

        bool free_entry() override;

        void insert_addr(uint64_t addr) override;

        uint64_t evict_addr() override;

        void update_status(uint64_t addr) override;

    public:

        explicit FIFO(uint64_t _MAX_SIZE) : EvictionPolicy(_MAX_SIZE) {}

    };

}

#endif //DNNSIM_FIFO_H

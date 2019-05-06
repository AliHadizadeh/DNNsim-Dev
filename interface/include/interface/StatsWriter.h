#ifndef DNNSIM_STATSWRITER_H
#define DNNSIM_STATSWRITER_H

#include <sys/common.h>
#include <sys/Statistics.h>

//#define PER_IMAGE_RESULTS
#define PER_EPOCH_RESULTS

namespace interface {

        class StatsWriter {

        private:

            /* Check if the path exists
             * @param path  Path we want to check
             */
            static void check_path(const std::string &path);

        public:

            /* Dump the statistics in a csv file */
            static void dump_csv();

        };

}


#endif //DNNSIM_STATSWRITER_H

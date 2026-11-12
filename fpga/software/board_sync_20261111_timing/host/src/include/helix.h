#ifndef HELIX_HELIX_H
#define HELIX_HELIX_H

#include "radio_control.h"
#include "streamer.h"
#include "udp_transport.h"

namespace helix {

    class helix {

        public:
            explicit helix(const char* _ip);
            streamer* stream;
            radio_control* control;
            cmd_manager* cmd;   // Claude 2026-11-11: made public (was private) so examples can use writeReg/readReg

            udp_transport* udp;  // Claude 2026-11-11: made public for the timing/stress programs

        private:
            const char* ip;
    };

} // helix

#endif //HELIX_HELIX_H

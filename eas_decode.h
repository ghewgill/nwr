#include <string>
#include <vector>

namespace eas {

struct Message {
    std::string originator;
    std::string originator_desc;
    std::string event;
    std::string event_desc;
    struct Area {
        std::string code;
        int part;
        int state;
        int county;
        std::string desc;
    };
    std::vector<Area> areas;
    time_t issued;
    time_t purge;
    std::string sender;
    std::string sender_desc;
};

bool Decode(const char *s, Message &message);

} // namespace eas

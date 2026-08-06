#ifndef SKETCHSYNC_MEMBER_H
#define SKETCHSYNC_MEMBER_H
#include <cstdint>
#include <string>

struct member
{
    uint32_t id;
    std::string name;

};

struct owner
{
    uint32_t id;
    std::string name;
    uint32_t session_id;
};

#endif

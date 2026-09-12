#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <deque>
#include <string>
#include <unordered_map>

#include "../ipc/transport.hpp"
#include "../input/input.hpp"
#include "../render/framebuffer.hpp"

namespace blockos::bx11::server {

struct Window
{
    uint32_t id{};
    uint32_t parent{};

    int x{};
    int y{};

    uint32_t width{};
    uint32_t height{};

    uint32_t border_width{};
    uint32_t background{};

    bool mapped{};
    bool override_redirect{};

    uint64_t event_mask{};

    /*
     * ICCCM state.
     */
    uint32_t wm_name_atom{};
    uint32_t wm_class_atom{};
    uint32_t wm_protocols_atom{};

    std::unordered_map<uint32_t, struct AtomValue> properties;
};

struct AtomValue
{
    uint32_t type{};
    uint8_t format{};

    std::vector<uint8_t> data;
};

struct Client
{
    ipc::Channel* channel{};

    uint32_t next_id{1};
    uint32_t sequence{0};

    std::deque<input::Event> events;
};

class Server
{
public:

    bool init(render::Surface* surface);

    uint32_t connect(ipc::Channel* channel);

    void disconnect(uint32_t client_id);

    bool process_one(uint32_t client_id);

    void render();


    // --------------------------------------------------------
    // Windows
    // --------------------------------------------------------

    uint32_t create_window(
        uint32_t client_id,
        uint32_t parent,
        int x,
        int y,
        uint32_t w,
        uint32_t h,
        uint32_t border,
        uint32_t background
    );

    bool map_window(uint32_t id);

    bool unmap_window(uint32_t id);

    bool destroy_window(uint32_t id);

    bool move_resize(
        uint32_t id,
        int x,
        int y,
        uint32_t w,
        uint32_t h
    );

    bool select_input(
        uint32_t id,
        uint64_t mask
    );


    // --------------------------------------------------------
    // Atoms
    // --------------------------------------------------------

    uint32_t intern_atom(
        const std::string& name,
        bool only_if_exists
    );

    const std::string* atom_name(
        uint32_t atom
    ) const;


    // --------------------------------------------------------
    // Properties
    // --------------------------------------------------------

    bool change_property(
        uint32_t window,
        uint32_t property,
        uint32_t type,
        uint8_t format,
        int mode,
        const void* data,
        size_t bytes
    );

    bool delete_property(
        uint32_t window,
        uint32_t property
    );

    bool get_property(
        uint32_t window,
        uint32_t property,
        uint32_t requested_type,
        long offset,
        long length,
        bool delete_property,
        uint32_t* actual_type,
        uint8_t* actual_format,
        unsigned long* nitems,
        unsigned long* bytes_after,
        std::vector<uint8_t>& data
    ) const;


    // --------------------------------------------------------
    // Input
    // --------------------------------------------------------

    bool push_input(
        uint32_t window,
        const input::Event& event
    );

    bool pop_event(
        uint32_t client_id,
        input::Event& event
    );


    // --------------------------------------------------------
    // Window lookup
    // --------------------------------------------------------

    const Window* find_window(
        uint32_t id
    ) const;

    Window* find_window(
        uint32_t id
    );


private:

    struct ClientSlot
    {
        uint32_t id{};
        Client client{};
    };


    render::Surface* surface_{};

    uint32_t root_{1};

    uint32_t next_window_id_{2};

    /*
     * Predefined atoms end at XA_WM_TRANSIENT_FOR.
     * Dynamic atoms start here.
     */
    uint32_t next_atom_id_{69};

    uint32_t next_client_id_{1};


    std::unordered_map<
        uint32_t,
        Window
    > windows_;


    std::unordered_map<
        uint32_t,
        ClientSlot
    > clients_;


    std::unordered_map<
        std::string,
        uint32_t
    > atoms_by_name_;


    std::unordered_map<
        uint32_t,
        std::string
    > atom_names_;
};

}

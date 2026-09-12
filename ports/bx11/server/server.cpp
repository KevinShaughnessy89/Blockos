#include "../include/X11/X.h"
#include "server.hpp"

#include <cstring>

namespace blockos::bx11::server {

// ============================================================
// INIT
// ============================================================

bool Server::init(
    render::Surface* surface)
{
    if (!surface)
        return false;

    surface_ = surface;

    windows_.clear();
    clients_.clear();

    atoms_by_name_.clear();
    atom_names_.clear();


    // --------------------------------------------------------
    // Root window
    // --------------------------------------------------------

    windows_[root_] =
        Window{
            root_,
            0,

            0,
            0,

            surface->framebuffer().width,
            surface->framebuffer().height,

            0,

            0xFF202020,

            true,
            false,

            0
        };


    // --------------------------------------------------------
    // ICCCM predefined atoms
    // --------------------------------------------------------

    atoms_by_name_["PRIMARY"] =
        XA_PRIMARY;

    atom_names_[XA_PRIMARY] =
        "PRIMARY";


    atoms_by_name_["SECONDARY"] =
        XA_SECONDARY;

    atom_names_[XA_SECONDARY] =
        "SECONDARY";


    atoms_by_name_["WM_NAME"] =
        XA_WM_NAME;

    atom_names_[XA_WM_NAME] =
        "WM_NAME";


    atoms_by_name_["WM_CLASS"] =
        XA_WM_CLASS;

    atom_names_[XA_WM_CLASS] =
        "WM_CLASS";


    atoms_by_name_["WM_PROTOCOLS"] =
        67;

    atom_names_[67] =
        "WM_PROTOCOLS";


    atoms_by_name_["WM_TRANSIENT_FOR"] =
        XA_WM_TRANSIENT_FOR;

    atom_names_[XA_WM_TRANSIENT_FOR] =
        "WM_TRANSIENT_FOR";


    atoms_by_name_["WM_NORMAL_HINTS"] =
        XA_WM_NORMAL_HINTS;

    atom_names_[XA_WM_NORMAL_HINTS] =
        "WM_NORMAL_HINTS";


    atoms_by_name_["WM_HINTS"] =
        XA_WM_HINTS;

    atom_names_[XA_WM_HINTS] =
        "WM_HINTS";


    atoms_by_name_["WM_ICON_NAME"] =
        XA_WM_ICON_NAME;

    atom_names_[XA_WM_ICON_NAME] =
        "WM_ICON_NAME";


    atoms_by_name_["WM_COMMAND"] =
        XA_WM_COMMAND;

    atom_names_[XA_WM_COMMAND] =
        "WM_COMMAND";


    atoms_by_name_["WM_CLIENT_MACHINE"] =
        XA_WM_CLIENT_MACHINE;

    atom_names_[XA_WM_CLIENT_MACHINE] =
        "WM_CLIENT_MACHINE";


    atoms_by_name_["WM_DELETE_WINDOW"] =
        68;

    atom_names_[68] =
        "WM_DELETE_WINDOW";


    // --------------------------------------------------------
    // EWMH atoms that are already useful to ICCCM clients
    // --------------------------------------------------------

    atoms_by_name_["_NET_SUPPORTED"] =
        69;

    atom_names_[69] =
        "_NET_SUPPORTED";


    atoms_by_name_["_NET_SUPPORTING_WM_CHECK"] =
        70;

    atom_names_[70] =
        "_NET_SUPPORTING_WM_CHECK";


    return true;
}


// ============================================================
// CLIENT
// ============================================================

uint32_t Server::connect(
    ipc::Channel* channel)
{
    if (!channel)
        return 0;

    uint32_t id =
        next_client_id_++;

    clients_[id] =
        ClientSlot{
            id,
            Client{
                channel,
                0,
                0,
                {}
            }
        };

    return id;
}


void Server::disconnect(
    uint32_t client_id)
{
    clients_.erase(client_id);
}


// ============================================================
// CREATE WINDOW
// ============================================================

uint32_t Server::create_window(
    uint32_t,
    uint32_t parent,
    int x,
    int y,
    uint32_t w,
    uint32_t h,
    uint32_t border,
    uint32_t background)
{
    if (!find_window(parent))
        return 0;

    if (w == 0 || h == 0)
        return 0;


    uint32_t id =
        next_window_id_++;


    Window window{};

    window.id =
        id;

    window.parent =
        parent;

    window.x =
        x;

    window.y =
        y;

    window.width =
        w;

    window.height =
        h;

    window.border_width =
        border;

    window.background =
        background;

    window.mapped =
        false;

    window.override_redirect =
        false;

    window.event_mask =
        0;


    windows_[id] =
        std::move(window);


    return id;
}


// ============================================================
// WINDOW LOOKUP
// ============================================================

Window* Server::find_window(
    uint32_t id)
{
    auto it =
        windows_.find(id);

    if (it ==
        windows_.end())
    {
        return nullptr;
    }

    return &it->second;
}


const Window* Server::find_window(
    uint32_t id) const
{
    auto it =
        windows_.find(id);

    if (it ==
        windows_.end())
    {
        return nullptr;
    }

    return &it->second;
}


// ============================================================
// MAP
// ============================================================

bool Server::map_window(
    uint32_t id)
{
    Window* w =
        find_window(id);

    if (!w)
        return false;

    w->mapped =
        true;

    return true;
}


// ============================================================
// UNMAP
// ============================================================

bool Server::unmap_window(
    uint32_t id)
{
    Window* w =
        find_window(id);

    if (!w)
        return false;

    w->mapped =
        false;

    return true;
}


// ============================================================
// DESTROY
// ============================================================

bool Server::destroy_window(
    uint32_t id)
{
    if (id == root_)
        return false;

    return windows_.erase(id) == 1;
}


// ============================================================
// MOVE / RESIZE
// ============================================================

bool Server::move_resize(
    uint32_t id,
    int x,
    int y,
    uint32_t w,
    uint32_t h)
{
    Window* win =
        find_window(id);

    if (!win)
        return false;

    if (w == 0 || h == 0)
        return false;


    win->x =
        x;

    win->y =
        y;

    win->width =
        w;

    win->height =
        h;


    return true;
}


// ============================================================
// SELECT INPUT
// ============================================================

bool Server::select_input(
    uint32_t id,
    uint64_t mask)
{
    Window* w =
        find_window(id);

    if (!w)
        return false;

    w->event_mask =
        mask;

    return true;
}


// ============================================================
// ATOMS
// ============================================================

uint32_t Server::intern_atom(
    const std::string& name,
    bool only_if_exists)
{
    auto it =
        atoms_by_name_.find(name);

    if (it !=
        atoms_by_name_.end())
    {
        return it->second;
    }


    if (only_if_exists)
        return 0;


    uint32_t id =
        next_atom_id_++;


    atoms_by_name_[name] =
        id;

    atom_names_[id] =
        name;


    return id;
}


const std::string* Server::atom_name(
    uint32_t atom) const
{
    auto it =
        atom_names_.find(atom);

    if (it ==
        atom_names_.end())
    {
        return nullptr;
    }

    return &it->second;
}


// ============================================================
// CHANGE PROPERTY
// ============================================================

bool Server::change_property(
    uint32_t window,
    uint32_t property,
    uint32_t type,
    uint8_t format,
    int mode,
    const void* data,
    size_t bytes)
{
    Window* w =
        find_window(window);

    if (!w)
        return false;

    if (property == 0)
        return false;

    if (format != 8 &&
        format != 16 &&
        format != 32)
    {
        return false;
    }


    AtomValue& value =
        w->properties[property];


    value.type =
        type;

    value.format =
        format;


    if (!data || bytes == 0)
    {
        if (mode ==
            PropModeReplace)
        {
            value.data.clear();
        }

        return true;
    }


    const uint8_t* src =
        static_cast<
            const uint8_t*
        >(data);


    switch (mode)
    {
        case PropModeReplace:
        {
            value.data.assign(
                src,
                src + bytes
            );

            break;
        }


        case PropModeAppend:
        {
            value.data.insert(
                value.data.end(),
                src,
                src + bytes
            );

            break;
        }


        case PropModePrepend:
        {
            std::vector<uint8_t>
                new_data;

            new_data.reserve(
                bytes +
                value.data.size()
            );


            new_data.insert(
                new_data.end(),
                src,
                src + bytes
            );


            new_data.insert(
                new_data.end(),
                value.data.begin(),
                value.data.end()
            );


            value.data =
                std::move(new_data);

            break;
        }


        default:
            return false;
    }


    return true;
}


// ============================================================
// DELETE PROPERTY
// ============================================================

bool Server::delete_property(
    uint32_t window,
    uint32_t property)
{
    Window* w =
        find_window(window);

    if (!w)
        return false;

    w->properties.erase(
        property
    );

    return true;
}


// ============================================================
// GET PROPERTY
// ============================================================

bool Server::get_property(
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
    std::vector<uint8_t>& data) const
{
    data.clear();


    if (actual_type)
        *actual_type = 0;

    if (actual_format)
        *actual_format = 0;

    if (nitems)
        *nitems = 0;

    if (bytes_after)
        *bytes_after = 0;


    const Window* w =
        find_window(window);

    if (!w)
        return false;


    auto it =
        w->properties.find(property);

    if (it ==
        w->properties.end())
    {
        return true;
    }


    const AtomValue& value =
        it->second;


    if (requested_type != 0 &&
        requested_type != value.type)
    {
        if (actual_type)
            *actual_type =
                value.type;

        if (actual_format)
            *actual_format =
                value.format;

        return true;
    }


    if (actual_type)
        *actual_type =
            value.type;

    if (actual_format)
        *actual_format =
            value.format;


    size_t unit =
        value.format / 8;


    if (unit == 0)
        return false;


    size_t start =
        static_cast<size_t>(
            offset < 0 ? 0 : offset
        ) * 4;


    if (start >
        value.data.size())
    {
        start =
            value.data.size();
    }


    size_t available =
        value.data.size() -
        start;


    size_t wanted =
        length < 0
            ? available
            : static_cast<size_t>(length) * 4;


    size_t count =
        available < wanted
            ? available
            : wanted;


    /*
     * Keep property data aligned to the element size.
     */
    count -=
        count % unit;


    data.assign(
        value.data.begin() + start,
        value.data.begin() + start + count
    );


    if (bytes_after)
        *bytes_after =
            static_cast<unsigned long>(
                available - count
            );


    if (nitems)
        *nitems =
            static_cast<unsigned long>(
                count / unit
            );


    if (delete_property &&
        count == available)
    {
        const_cast<Server*>(this)
            ->delete_property(
                window,
                property
            );
    }


    return true;
}


// ============================================================
// INPUT
// ============================================================

bool Server::push_input(
    uint32_t window,
    const input::Event& event)
{
    Window* w =
        find_window(window);

    if (!w)
        return false;


    for (auto& [cid, slot] :
         clients_)
    {
        (void)cid;

        if (w->event_mask != 0)
        {
            slot.client.events.push_back(
                event
            );
        }
    }


    return true;
}


bool Server::pop_event(
    uint32_t client_id,
    input::Event& event)
{
    auto it =
        clients_.find(client_id);

    if (it ==
        clients_.end())
    {
        return false;
    }


    auto& queue =
        it->second.client.events;


    if (queue.empty())
        return false;


    event =
        queue.front();

    queue.pop_front();

    return true;
}


// ============================================================
// PROCESS
// ============================================================

bool Server::process_one(
    uint32_t)
{
    return false;
}


// ============================================================
// RENDER
// ============================================================

void Server::render()
{
    if (!surface_)
        return;


    surface_->clear(
        0xFF1E1E1E
    );


    for (const auto& [id, w] :
         windows_)
    {
        if (id == root_)
            continue;

        if (!w.mapped)
            continue;


        surface_->rect(
            w.x,
            w.y,
            static_cast<int>(
                w.width
            ),
            static_cast<int>(
                w.height
            ),
            w.background
        );
    }
}

}

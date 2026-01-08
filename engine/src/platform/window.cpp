#include "pnkr/platform/window.hpp"
#include "pnkr/core/logger.hpp"
#include <cpptrace/cpptrace.hpp>
#include <SDL3/SDL_properties.h>

namespace pnkr::platform
{
    Window::Window(const std::string& title, int width, int height,
                   SDL_WindowFlags flags)
    {
        SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        {
            throw cpptrace::runtime_error(std::string("SDL_InitSubSystem(VIDEO) failed: ") + SDL_GetError());
        }

        SDL_Window* rawWindow =
            SDL_CreateWindow(title.c_str(), width, height,
                             flags | SDL_WINDOW_VULKAN
            );

        if (rawWindow == nullptr)
        {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            throw cpptrace::runtime_error(std::string("SDL_CreateWindow failed: ") +
                SDL_GetError());
        }

        m_window.reset(rawWindow);

        int w;
        int h;
        SDL_GetWindowSize(m_window.get(), &w, &h);
        SDL_DisplayID display = SDL_GetDisplayForWindow(m_window.get());
        const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(display);
        float refresh = (mode != nullptr) ? mode->refresh_rate : 60.0F;

        pnkr::core::Logger::Platform.info("Window created: {}x{} @ {:.1f}Hz (Title: '{}')", w, h, refresh, title);
    }

    Window::~Window()
    {
        m_window.reset();
    }

    void Window::processEvents(Input* input, const EventCallback& callback) noexcept
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (callback) {
                callback(event);
            }

            if (input != nullptr)
            {
                input->processEvent(event);
            }

            switch (event.type)
            {
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                m_running = false;
                break;

            case SDL_EVENT_WINDOW_RESIZED:
                pnkr::core::Logger::Platform.debug("Window resized to {}x{}", event.window.data1,
                                          event.window.data2);
                break;

            case SDL_EVENT_WINDOW_MINIMIZED:
                pnkr::core::Logger::Platform.debug("Window minimized");
                break;

            case SDL_EVENT_WINDOW_RESTORED:
                pnkr::core::Logger::Platform.debug("Window restored");
                break;

            default:
                break;
            }
        }
    }

    void* Window::nativeHandle() const noexcept
    {
#ifdef _WIN32

      SDL_PropertiesID props = SDL_GetWindowProperties(m_window.get());
      return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER,
                                    nullptr);
#else
      return nullptr;
#endif
    }

    void Window::setTitle(const std::string& title) const
    {
        SDL_SetWindowTitle(m_window.get(), title.c_str());
    }

    int Window::width() const noexcept
    {
        int width = 0;
        SDL_GetWindowSize(m_window.get(), &width, nullptr);
        return width;
    }

    int Window::height() const noexcept
    {
        int height = 0;
        SDL_GetWindowSize(m_window.get(), nullptr, &height);
        return height;
    }
}

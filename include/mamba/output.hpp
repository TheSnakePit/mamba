// Copyright (c) 2019, QuantStack and Mamba Contributors
//
// Distributed under the terms of the BSD 3-Clause License.
//
// The full license is in the file LICENSE, distributed with this software.

#ifndef MAMBA_OUTPUT_HPP
#define MAMBA_OUTPUT_HPP

#include "context.hpp"
#include "util.hpp"

#include "nlohmann/json.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>


#define ENUM_FLAG_OPERATOR(T, X)                                                                   \
    inline T operator X(T lhs, T rhs)                                                              \
    {                                                                                              \
        return (T)(static_cast<std::underlying_type_t<T>>(lhs)                                     \
                       X static_cast<std::underlying_type_t<T>>(rhs));                             \
    }
#define ENUM_FLAGS(T)                                                                              \
    enum class T;                                                                                  \
    inline T operator~(T t)                                                                        \
    {                                                                                              \
        return (T)(~static_cast<std::underlying_type_t<T>>(t));                                    \
    }                                                                                              \
    ENUM_FLAG_OPERATOR(T, |)                                                                       \
    ENUM_FLAG_OPERATOR(T, ^)                                                                       \
    ENUM_FLAG_OPERATOR(T, &)                                                                       \
    enum class T

#define PREFIX_LENGTH 25

namespace mamba
{
    const char banner[] =
        R"MAMBARAW(                                        __
            _____ ___  ____ _____ ___  / /_  ____ _
            / __ `__ \/ __ `/ __ `__ \/ __ \/ __ `/
           / / / / / / /_/ / / / / / / /_/ / /_/ /
          /_/ /_/ /_/\__,_/_/ /_/ /_/_.___/\__,_/
    )MAMBARAW";
}

namespace cursor
{
    class CursorMovementTriple
    {
    public:
        CursorMovementTriple(const char* esc, int n, const char* mod)
            : m_esc(esc)
            , m_mod(mod)
            , m_n(n)
        {
        }

        const char* m_esc;
        const char* m_mod;
        int m_n;
    };

    inline std::ostream& operator<<(std::ostream& o, const CursorMovementTriple& m)
    {
        o << m.m_esc << m.m_n << m.m_mod;
        return o;
    }

    class CursorMod
    {
    public:
        CursorMod(const char* mod)
            : m_mod(mod)
        {
        }

        std::ostream& operator<<(std::ostream& o)
        {
            o << m_mod;
            return o;
        }

        const char* m_mod;
    };

    inline auto up(int n)
    {
        return CursorMovementTriple("\x1b[", n, "A");
    }

    inline auto down(int n)
    {
        return CursorMovementTriple("\x1b[", n, "B");
    }

    inline auto forward(int n)
    {
        return CursorMovementTriple("\x1b[", n, "C");
    }

    inline auto back(int n)
    {
        return CursorMovementTriple("\x1b[", n, "D");
    }

    inline auto next_line(int n)
    {
        return CursorMovementTriple("\x1b[", n, "E");
    }

    inline auto prev_line(int n)
    {
        return CursorMovementTriple("\x1b[", n, "F");
    }

    inline auto horizontal_abs(int n)
    {
        return CursorMovementTriple("\x1b[", n, "G");
    }

    inline auto erase_line(int n = 0)
    {
        return CursorMovementTriple("\x1b[", n, "K");
    }

    inline auto show(int n)
    {
        return CursorMod("\x1b[?25h");
    }

    inline auto hide(int n)
    {
        return CursorMod("\x1b[?25l");
    }
}  // namespace cursor

namespace mamba
{
    std::string cut_repo_name(const std::string& reponame);

    namespace printers
    {
        enum class format : std::size_t
        {
            none = 0,
            red = 1 << 1,
            green = 1 << 2,
            yellow = 1 << 3
        };

        struct FormattedString
        {
            std::string s;
            format flag = format::none;

            FormattedString() = default;

            inline FormattedString(const std::string& i)
                : s(i)
            {
            }

            inline FormattedString(const char* i)
                : s(i)
            {
            }

            inline std::size_t size() const
            {
                return s.size();
            }
        };

        enum class alignment : std::size_t
        {
            left = 1 << 1,
            right = 1 << 2,
            fill = 1 << 3
        };

        class Table
        {
        public:
            Table(const std::vector<FormattedString>& header);

            void set_alignment(const std::vector<alignment>& a);
            void set_padding(const std::vector<int>& p);
            void add_row(const std::vector<FormattedString>& r);
            void add_rows(const std::string& header,
                          const std::vector<std::vector<FormattedString>>& rs);

            std::ostream& print(std::ostream& out);

        private:
            std::vector<FormattedString> m_header;
            std::vector<alignment> m_align;
            std::vector<int> m_padding;
            std::vector<std::vector<FormattedString>> m_table;
        };
    }  // namespace printers

    // The next two functions / classes were ported from the awesome indicators
    // library by p-ranav (MIT License) https://github.com/p-ranav/indicators
    std::ostream& write_duration(std::ostream& os, std::chrono::nanoseconds ns);
    int get_console_width();

    class ProgressScaleWriter
    {
    public:
        inline ProgressScaleWriter(int bar_width,
                                   const std::string& fill,
                                   const std::string& lead,
                                   const std::string& remainder);

        std::ostream& write(std::ostream& os, std::size_t progress) const;

    private:
        int m_bar_width;
        std::string m_fill;
        std::string m_lead;
        std::string m_remainder;
    };

    class ProgressBar
    {
    public:
        ProgressBar(const std::string& prefix);

        void set_start();
        void set_progress(char p);
        void set_postfix(const std::string& postfix_text);
        void print();
        void mark_as_completed();
        void elapsed_time_to_stream(std::stringstream& s);
        const std::string& prefix() const;

    private:
        std::chrono::nanoseconds m_elapsed_ns;
        std::chrono::time_point<std::chrono::high_resolution_clock> m_start_time;

        std::string m_prefix, m_postfix;
        bool m_start_time_saved, m_activate_bob;
        char m_progress = 0;
    };

    class ProgressProxy
    {
    public:
        ProgressProxy() = default;
        ~ProgressProxy() = default;

        ProgressProxy(const ProgressProxy&) = default;
        ProgressProxy& operator=(const ProgressProxy&) = default;
        ProgressProxy(ProgressProxy&&) = default;
        ProgressProxy& operator=(ProgressProxy&&) = default;

        void set_progress(char p);
        void elapsed_time_to_stream(std::stringstream& s);
        void set_postfix(const std::string& s);
        void mark_as_completed(const std::string_view& final_message = "");

    private:
        ProgressProxy(ProgressBar* ptr, std::size_t idx);

        ProgressBar* p_bar;
        std::size_t m_idx;

        friend class Console;
    };

    // Todo: replace public inheritance with
    // private one + using directives
    class ConsoleStream : public std::stringstream
    {
    public:
        ConsoleStream() = default;
        ~ConsoleStream();
    };

    class Console
    {
    public:
        Console(const Console&) = delete;
        Console& operator=(const Console&) = delete;

        Console(Console&&) = delete;
        Console& operator=(Console&&) = delete;

        static Console& instance();

        static ConsoleStream stream();
        static void print(const std::string_view& str, bool force_print = false);
        static bool prompt(const std::string_view& message, char fallback = '_');

        ProgressProxy add_progress_bar(const std::string& name);
        void init_multi_progress();

    private:
        using progress_bar_ptr = std::unique_ptr<ProgressBar>;

        Console();
        ~Console() = default;

        void deactivate_progress_bar(std::size_t idx, const std::string_view& msg = "");
        void print_progress(std::size_t idx);
        void print_progress();
        void print_progress_unlocked();
        bool skip_progress_bars() const;

        std::mutex m_mutex;
        std::vector<progress_bar_ptr> m_progress_bars;
        std::vector<ProgressBar*> m_active_progress_bars;
        bool m_progress_started = false;

        friend class ProgressProxy;
    };

    inline void ProgressProxy::set_postfix(const std::string& s)
    {
        p_bar->set_postfix(s);
        Console::instance().print_progress(m_idx);
    }

#undef DEBUG
#undef INFO
#undef WARNING
#undef ERROR
#undef FATAL


    class MessageLogger
    {
    public:
        MessageLogger(const char* file, int line, LogLevel severity);
        ~MessageLogger();

        std::stringstream& stream();

        static LogLevel& global_log_level();

    private:
        std::string m_file;
        int m_line;
        LogLevel m_level;
        std::stringstream m_stream;
    };

    class JsonLogger
    {
    public:
        JsonLogger(const JsonLogger&) = delete;
        JsonLogger& operator=(const JsonLogger&) = delete;

        JsonLogger(JsonLogger&&) = delete;
        JsonLogger& operator=(JsonLogger&&) = delete;

        static JsonLogger& instance();

        nlohmann::json json_log;
        void json_write(const nlohmann::json& j);
        void json_append(const std::string& value);
        void json_append(const nlohmann::json& j);
        void json_down(const std::string& key);
        void json_up();

    private:
        JsonLogger();
        ~JsonLogger() = default;

        std::string json_hier;
        unsigned int json_index;
    };
}  // namespace mamba

#undef ERROR
#undef WARNING
#undef FATAL

#define LOG(severity) mamba::MessageLogger(__FILE__, __LINE__, severity).stream()
#define LOG_TRACE LOG(mamba::LogLevel::kTrace)
#define LOG_DEBUG LOG(mamba::LogLevel::kDebug)
#define LOG_INFO LOG(mamba::LogLevel::kInfo)
#define LOG_WARNING LOG(mamba::LogLevel::kWarning)
#define LOG_ERROR LOG(mamba::LogLevel::kError)
#define LOG_FATAL LOG(mamba::LogLevel::kFatal)

#endif  // MAMBA_OUTPUT_HPP

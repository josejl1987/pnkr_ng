#pragma once
#include <string>
#include <string_view>
#include <functional>
#include <unordered_map>
#include <atomic>
#include <type_traits>

#include <filesystem>

namespace pnkr::core {

enum class CVarFlags : uint32_t {
    none = 0,
    save = 1 << 0,
    cheat = 1 << 1,
    read_only = 1 << 2,
    restart = 1 << 3,
};

inline CVarFlags operator|(CVarFlags a, CVarFlags b) {
    return static_cast<CVarFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline bool operator&(CVarFlags a, CVarFlags b) {
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

struct ICVar {
    std::string name;
    std::string description;
    CVarFlags flags;
    virtual ~ICVar() = default;
    virtual std::string toString() const = 0;
    virtual void setFromString(const std::string& val) = 0;
};

class CVarSystem {
public:
    static void registerCVar(ICVar* cvar);
    static ICVar* find(const std::string& name);
    static std::unordered_map<std::string, ICVar*>& getAll();

    static void saveToIni(const std::filesystem::path& path);
    static void loadFromIni(const std::filesystem::path& path);
};

template <typename T>
class CVar : public ICVar {
    static_assert(std::is_arithmetic_v<T>, "Generic CVar only supports arithmetic types. Use specializations for others.");
public:
    using OnChangeFunc = std::function<void(T)>;

    CVar(const char* name, const char* desc, T defaultValue, CVarFlags flags = CVarFlags::none, OnChangeFunc onChange = nullptr)
        : m_onChange(onChange)
    {
        this->name = name;
        this->description = desc;
        this->flags = flags;
        m_value.store(defaultValue, std::memory_order_relaxed);
        CVarSystem::registerCVar(this);
    }

    T get() const {
        return m_value.load(std::memory_order_relaxed);
    }

    void set(T val) {
        m_value.store(val, std::memory_order_relaxed);
        if (m_onChange) m_onChange(val);
    }

    std::string toString() const override {
        return std::to_string(get());
    }

    void setFromString(const std::string& val) override {
        T newValue{};
        if constexpr (std::is_same_v<T, float>) {
            newValue = std::stof(val);
        } else if constexpr (std::is_same_v<T, double>) {
            newValue = std::stod(val);
        } else if constexpr (std::is_same_v<T, int>) {
            newValue = std::stoi(val);
        } else if constexpr (std::is_same_v<T, bool>) {
            newValue = (val == "1" || val == "true" || val == "True");
        } else {

             if constexpr (std::is_floating_point_v<T>) newValue = static_cast<T>(std::stof(val));
             else newValue = static_cast<T>(std::stoi(val));
        }
        set(newValue);
    }

private:
    std::atomic<T> m_value;
    OnChangeFunc m_onChange;
};

template <>
class CVar<std::string> : public ICVar {
public:
    using OnChangeFunc = std::function<void(std::string)>;

    CVar(const char* name, const char* desc, std::string defaultValue, CVarFlags flags = CVarFlags::none, OnChangeFunc onChange = nullptr);

    std::string get() const;
    void set(std::string val);

    std::string toString() const override;
    void setFromString(const std::string& val) override;

private:
    std::string m_value;
    OnChangeFunc m_onChange;
};

#define AUTO_CVAR_FLOAT(Name, Desc, Default, ...) pnkr::core::CVar<float> Name(#Name, Desc, Default, ##__VA_ARGS__)
#define AUTO_CVAR_INT(Name, Desc, Default, ...) pnkr::core::CVar<int> Name(#Name, Desc, Default, ##__VA_ARGS__)
#define AUTO_CVAR_BOOL(Name, Desc, Default, ...) pnkr::core::CVar<bool> Name(#Name, Desc, Default, ##__VA_ARGS__)
#define AUTO_CVAR_STRING(Name, Desc, Default, ...) pnkr::core::CVar<std::string> Name(#Name, Desc, Default, ##__VA_ARGS__)

}

#pragma once

#include "mods/api.h"

#include <cstdio>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

namespace dusk::mods {

template <class Service>
struct ServiceTraits;

namespace detail {

inline std::vector<ServiceImport>& imports() {
    static std::vector<ServiceImport> entries;
    return entries;
}

inline std::vector<ServiceExport>& exports() {
    static std::vector<ServiceExport> entries;
    return entries;
}

inline int register_import(ServiceImport entry) {
    imports().push_back(entry);
    return 0;
}

inline int register_export(ServiceExport entry) {
    exports().push_back(entry);
    return 0;
}

inline const ModManifest* manifest() {
    static ModManifest manifest{
        sizeof(ModManifest),
        MOD_ABI_VERSION,
        nullptr,
        0,
        nullptr,
        0,
    };

    auto& importEntries = imports();
    auto& exportEntries = exports();
    manifest.imports = importEntries.data();
    manifest.import_count = importEntries.size();
    manifest.exports = exportEntries.data();
    manifest.export_count = exportEntries.size();
    return &manifest;
}

}  // namespace detail

inline ModResult set_error(ModError* outError, ModResult code, const char* message) {
    if (outError != nullptr && outError->struct_size >= sizeof(ModError)) {
        outError->code = code;
        outError->message[0] = '\0';
        if (message != nullptr) {
            std::snprintf(outError->message, sizeof(outError->message), "%s", message);
        }
    }
    return code;
}

template <class Service>
class ServiceRef {
public:
    constexpr ServiceRef() = default;

    explicit constexpr ServiceRef(const Service* const* slot) : m_slot{slot} {}

    [[nodiscard]] const Service* get() const {
        return m_slot != nullptr ? *m_slot : nullptr;
    }

    [[nodiscard]] const Service* const* slot() const {
        return m_slot;
    }

    explicit operator bool() const {
        return get() != nullptr;
    }

    template <auto method, class... Args>
    decltype(auto) call(Args&&... args) const {
        using FnPtr = decltype(std::declval<const Service>().*method);
        using Return = std::invoke_result_t<FnPtr, ModContext*, Args...>;

        const Service* service = get();
        const auto fn = service != nullptr ? service->*method : nullptr;
        if (fn == nullptr) {
            if constexpr (std::is_same_v<Return, ModResult>) {
                return MOD_UNAVAILABLE;
            } else if constexpr (std::is_void_v<Return>) {
                return;
            } else {
                return Return{};
            }
        }

        return fn(::mod_context, std::forward<Args>(args)...);
    }

private:
    const Service* const* m_slot = nullptr;
};

template <auto method, class Service, class... Args>
decltype(auto) call_service(const ServiceRef<Service>& service, Args&&... args) {
    return service.template call<method>(std::forward<Args>(args)...);
}

}  // namespace dusk::mods

#define DEFINE_MOD()                                                                              \
    extern "C" MOD_EXPORT ModContext* mod_context = nullptr;                                      \
    extern "C" MOD_EXPORT const ModManifest* mod_get_manifest(void) {                             \
        return ::dusk::mods::detail::manifest();                                                  \
    }

#define IMPORT_SERVICE_EX(                                                                        \
    service_type, variable, service_id_value, major_value, min_minor_value, flags_value)           \
    namespace {                                                                                   \
    const service_type* mod_import_slot_##variable = nullptr;                                      \
    const int mod_import_registration_##variable = ::dusk::mods::detail::register_import(          \
        ServiceImport{                                                                            \
            sizeof(ServiceImport),                                                                \
            (service_id_value),                                                                   \
            static_cast<uint16_t>(major_value),                                                   \
            static_cast<uint16_t>(min_minor_value),                                               \
            static_cast<uint32_t>(flags_value),                                                   \
            &mod_import_slot_##variable,                                                          \
        });                                                                                       \
    }                                                                                             \
    static const ::dusk::mods::ServiceRef<service_type> variable{&mod_import_slot_##variable}

#define IMPORT_SERVICE_VERSION(service_type, variable, min_minor_value)                            \
    IMPORT_SERVICE_EX(                                                                            \
        service_type,                                                                             \
        variable,                                                                                 \
        ::dusk::mods::ServiceTraits<service_type>::id,                                            \
        ::dusk::mods::ServiceTraits<service_type>::major_version,                                 \
        min_minor_value,                                                                          \
        SERVICE_IMPORT_REQUIRED)

#define IMPORT_SERVICE(service_type, variable)                                                     \
    IMPORT_SERVICE_VERSION(service_type, variable, 0)

#define IMPORT_OPTIONAL_SERVICE_VERSION(service_type, variable, min_minor_value)                   \
    IMPORT_SERVICE_EX(                                                                            \
        service_type,                                                                             \
        variable,                                                                                 \
        ::dusk::mods::ServiceTraits<service_type>::id,                                            \
        ::dusk::mods::ServiceTraits<service_type>::major_version,                                 \
        min_minor_value,                                                                          \
        SERVICE_IMPORT_OPTIONAL)

#define IMPORT_OPTIONAL_SERVICE(service_type, variable)                                            \
    IMPORT_OPTIONAL_SERVICE_VERSION(service_type, variable, 0)

#define EXPORT_SERVICE_AS(instance, service_id_value)                                              \
    namespace {                                                                                   \
    const int mod_export_registration_##instance = ::dusk::mods::detail::register_export(          \
        ServiceExport{                                                                            \
            sizeof(ServiceExport),                                                                \
            (service_id_value),                                                                   \
            (instance).header.major_version,                                                      \
            (instance).header.minor_version,                                                      \
            SERVICE_EXPORT_STATIC,                                                                \
            &(instance),                                                                          \
        });                                                                                       \
    }

#define EXPORT_SERVICE(instance)                                                                   \
    EXPORT_SERVICE_AS(                                                                             \
        instance,                                                                                  \
        ::dusk::mods::ServiceTraits<std::remove_cv_t<decltype(instance)>>::id)

#define EXPORT_DEFERRED_SERVICE(token, service_id_value, major_value, minor_value)                 \
    namespace {                                                                                   \
    const int mod_deferred_export_registration_##token =                                           \
        ::dusk::mods::detail::register_export(                                                    \
            ServiceExport{                                                                        \
                sizeof(ServiceExport),                                                            \
                (service_id_value),                                                               \
                static_cast<uint16_t>(major_value),                                               \
                static_cast<uint16_t>(minor_value),                                               \
                SERVICE_EXPORT_DEFERRED,                                                          \
                nullptr,                                                                          \
            });                                                                                   \
    }

#define SERVICE_CALL(service_ref, method, ...)                                                     \
    ::dusk::mods::call_service<                                                                   \
        &std::remove_cv_t<std::remove_pointer_t<decltype((service_ref).get())>>::method>(         \
        (service_ref) __VA_OPT__(, ) __VA_ARGS__)

#define SERVICE_HAS_FIELD(service_ref, field)                                                     \
    SERVICE_HAS(                                                                                  \
        (service_ref).get(),                                                                      \
        std::remove_cv_t<std::remove_pointer_t<decltype((service_ref).get())>>,                   \
        field)

// copyright defined in LICENSE.txt

#include <appbase/application.hpp>

#include <fc/exception/exception.hpp>
#include <fc/filesystem.hpp>
#include <fc/log/appender.hpp>
#include <fc/log/logger_config.hpp>
#include <fc/crypto/hex.hpp>

#include <eosio/version/version.hpp>

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/exception/diagnostic_information.hpp>

#ifdef INCLUDE_FILL_PG_PLUGIN
#include "fill_pg_plugin.hpp"
#endif

#ifdef INCLUDE_WASM_QL_PG_PLUGIN
#include "wasm_ql_pg_plugin.hpp"
#endif

#ifdef INCLUDE_FILL_ROCKSDB_PLUGIN
#include "fill_rocksdb_plugin.hpp"
#endif

#ifdef INCLUDE_WASM_QL_ROCKSDB_PLUGIN
#include "wasm_ql_rocksdb_plugin.hpp"
#endif

using namespace appbase;
namespace bfs = boost::filesystem;

namespace fc {
std::unordered_map<std::string, appender::ptr>& get_appender_map();
}

namespace detail {

void configure_logging(const bfs::path& config_path) {
    try {
        try {
            fc::configure_logging(config_path);
        } catch (...) {
            elog("Error reloading logging.json");
            throw;
        }
    } catch (const fc::exception& e) {
        elog("${e}", ("e", e.to_detail_string()));
    } catch (const boost::exception& e) {
        elog("${e}", ("e", boost::diagnostic_information(e)));
    } catch (const std::exception& e) {
        elog("${e}", ("e", e.what()));
    } catch (...) {
        // empty
    }
}

} // namespace detail

void logging_conf_handler() {
   auto config_path = app().get_logging_conf();
   if (fc::exists(config_path)) {
      ilog("Received HUP.  Reloading logging configuration from ${p}.", ("p", config_path.string()));
   } else {
      ilog("Received HUP.  No log config found at ${p}, setting to default.", ("p", config_path.string()));
   }
   ::detail::configure_logging(config_path);
   fc::log_config::initialize_appenders(app().get_io_service());
}

void initialize_logging() {
    auto config_path = app().get_logging_conf();
    if (fc::exists(config_path))
        fc::configure_logging(config_path); // intentionally allowing exceptions to escape
    fc::log_config::initialize_appenders(app().get_io_service());

    app().set_sighup_callback(logging_conf_handler);
}

enum return_codes {
    other_fail      = -2,
    initialize_fail = -1,
    success         = 0,
    bad_alloc       = 1,
};

int main(int argc, char** argv) {
    try {
        auto root = fc::app_path();
        app().set_default_data_dir(root / "flon/" APP_NAME "/data");
        app().set_default_config_dir(root / "flon/" APP_NAME "/config");

        uint32_t short_hash = 0;
        fc::from_hex(eosio::version::version_hash(), (char*)&short_hash, sizeof(short_hash));

        app().set_version(htonl(short_hash));
        app().set_version_string(eosio::version::version_client());
        app().set_full_version_string(eosio::version::version_full());

        if (!app().initialize<DEFAULT_PLUGINS>(argc, argv))
            return initialize_fail;
        initialize_logging();

        ilog(APP_NAME " version ${ver}", ("ver", app().version_string()));
        ilog(APP_NAME " using configuration file ${c}", ("c", app().full_config_file_path().string()));
        ilog(APP_NAME " data directory is ${d}", ("d", app().data_dir().string()));
        app().startup();
        app().exec();
    } catch (const fc::exception& e) {
        elog("${e}", ("e", e.to_detail_string()));
        return other_fail;
    } catch (const boost::interprocess::bad_alloc& e) {
        elog("bad alloc");
        return bad_alloc;
    } catch (const boost::exception& e) {
        elog("${e}", ("e", boost::diagnostic_information(e)));
        return other_fail;
    } catch (const std::runtime_error& e) {
        elog("${e}", ("e", e.what()));
        return other_fail;
    } catch (const std::exception& e) {
        elog("${e}", ("e", e.what()));
        return other_fail;
    } catch (...) {
        elog("unknown exception");
        return other_fail;
    }

    return success;
}

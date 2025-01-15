// copyright defined in LICENSE.txt

#include "wasm_ql_rocksdb_plugin.hpp"
#include "util.hpp"

#include <fc/exception/exception.hpp>

using namespace appbase;
namespace kv  = state_history::kv;
namespace rdb = state_history::rdb;

static auto& _wasm_ql_rocksdb_plugin = app().register_plugin<wasm_ql_rocksdb_plugin>();

struct rocksdb_database_interface : database_interface, std::enable_shared_from_this<rocksdb_database_interface> {
    std::shared_ptr<::rocksdb_inst> rocksdb_inst;

    virtual ~rocksdb_database_interface() {}

    virtual std::unique_ptr<query_session> create_query_session();
};

struct rocksdb_query_session : query_session {
    std::shared_ptr<rocksdb_database_interface> db_iface;
    state_history::fill_status                  fill_status;
    std::unique_ptr<rocksdb::Iterator>          it_for_get;
    std::unique_ptr<rocksdb::Iterator>          it0;
    std::unique_ptr<rocksdb::Iterator>          it1;
    std::unique_ptr<rocksdb::Iterator>          it2;
    std::unique_ptr<rocksdb::Iterator>          it3;
    std::unique_ptr<rocksdb::Iterator>          it4;

    rocksdb_query_session(const std::shared_ptr<rocksdb_database_interface>& db_iface)
        : db_iface(db_iface)
        , it_for_get{db_iface->rocksdb_inst->database.db->NewIterator(rocksdb::ReadOptions())}
        , it0{db_iface->rocksdb_inst->database.db->NewIterator(rocksdb::ReadOptions())}
        , it1{db_iface->rocksdb_inst->database.db->NewIterator(rocksdb::ReadOptions())}
        , it2{db_iface->rocksdb_inst->database.db->NewIterator(rocksdb::ReadOptions())}
        , it3{db_iface->rocksdb_inst->database.db->NewIterator(rocksdb::ReadOptions())}
        , it4{db_iface->rocksdb_inst->database.db->NewIterator(rocksdb::ReadOptions())} {

        auto f = rdb::get<state_history::fill_status>(*it_for_get, kv::make_fill_status_key(), false);
        if (f)
            fill_status = *f;
    }

    virtual ~rocksdb_query_session() {}

    virtual state_history::fill_status get_fill_status() override { return fill_status; }

    virtual std::optional<abieos::checksum256> get_block_id(uint32_t block_num) override {
        auto rb = rdb::get<kv::received_block>(*it_for_get, kv::make_received_block_key(block_num), false);
        if (rb)
            return rb->block_id;
        return {};
    }

    void append_fields(
        std::vector<char>& dest, abieos::input_buffer src, const std::vector<kv::key>& keys,
        std::vector<std::optional<uint32_t>>& positions, bool xform_key) {

        for (auto& key : keys) {
            auto pos = positions.at(key.field->field_index);
            if (!pos)
                throw std::runtime_error("key " + key.name + " has unknown position");
            if (*pos > src.end - src.pos)
                throw std::runtime_error("key position is out of range");
            abieos::input_buffer key_pos{src.pos + *pos, src.end};
            if (xform_key)
                key.field->type_obj->bin_to_key(dest, key_pos);
            else
                key.field->type_obj->bin_to_bin(dest, key_pos);
        }
    }

    virtual std::vector<char> query_database(abieos::input_buffer query_bin, uint32_t head) override {
        abieos::name query_name;
        abieos::bin_to_native(query_name, query_bin);

        // todo: check for false positives in secondary indexes
        // todo: check if index is populated in rdb
        // todo: clamp snapshot_block_num to first?
        auto it = db_iface->rocksdb_inst->query_config->query_map.find(query_name);
        if (it == db_iface->rocksdb_inst->query_config->query_map.end())
            throw std::runtime_error("query_database: unknown query: " + (std::string)query_name);
        auto& query = *it->second;
        if (!query.arg_types.empty())
            throw std::runtime_error("query_database: query: " + (std::string)query_name + " not implemented");

        uint32_t snapshot_block_num = 0;
        if (query.has_block_snapshot)
            snapshot_block_num = std::min(head, abieos::bin_to_native<uint32_t>(query_bin));

        auto first = kv::make_index_key(query.table_obj->short_name, query.index_obj->short_name);
        auto last  = first;

        auto add_fields = [&](auto& dest, auto& types) {
            for (auto& type : types)
                type.query_to_key(dest, query_bin);
        };
        add_fields(first, query.index_obj->range_types);
        add_fields(last, query.index_obj->range_types);

        auto max_results = std::min(abieos::read_raw<uint32_t>(query_bin), query.max_results);

        std::vector<std::vector<char>> rows;
        uint32_t                       num_results = 0;
        rdb::for_each_subkey(*it0, first, last, [&](const auto& index_key, auto, auto) {
            std::vector index_key_limit_block = index_key;
            if (query.table_obj->is_delta)
                kv::append_index_suffix(index_key_limit_block, snapshot_block_num);
            // todo: unify rdb's and pg's handling of negative result because of snapshot_block_num
            rdb::for_each(*it1, index_key_limit_block, index_key, [&](auto index_value, auto) {
                auto delta_value =
                    *rdb::get_raw(*it2, extract_pk_from_index(index_value, *query.table_obj, query.index_obj->sort_keys), true);
                rows.emplace_back(delta_value.pos, delta_value.end);
                if (query.join_table) {
                    auto join_key = kv::make_index_key(query.join_table->short_name, query.join_query_short_name);
                    std::vector<std::optional<uint32_t>> table_positions;
                    kv::init_positions(table_positions, query.table_obj->fields.size());
                    fill_positions(delta_value, query.table_obj->fields, table_positions);
                    bool found_join = false;
                    if (keys_have_positions(query.join_key_values, table_positions)) {
                        append_fields(join_key, delta_value, query.join_key_values, table_positions, true);
                        auto join_key_limit_block = join_key;
                        if (query.join_query->table_obj->is_delta)
                            kv::append_index_suffix(join_key_limit_block, snapshot_block_num);
                        auto& row = rows.back();
                        rdb::for_each(*it3, join_key_limit_block, join_key, [&](auto join_index_value, auto) {
                            found_join            = true;
                            auto join_delta_value = *rdb::get_raw(
                                *it4, extract_pk_from_index(join_index_value, *query.join_table, query.join_query->index_obj->sort_keys),
                                true);
                            std::vector<std::optional<uint32_t>> join_positions;
                            kv::init_positions(join_positions, query.join_table->fields.size());
                            fill_positions(join_delta_value, query.join_table->fields, join_positions);
                            append_fields(row, join_delta_value, query.fields_from_join, join_positions, false);
                            return false;
                        });
                    }
                    if (!found_join)
                        for (auto& field : query.join_table->fields)
                            field.type_obj->fill_empty(rows.back());
                }
                return false;
            });
            return ++num_results < max_results;
        });

        auto result = abieos::native_to_bin(rows);
        if ((uint32_t)result.size() != result.size())
            throw std::runtime_error("query_database: result is too big");
        return result;
    }
}; // rocksdb_query_session

std::unique_ptr<query_session> rocksdb_database_interface::create_query_session() {
    auto session = std::make_unique<rocksdb_query_session>(shared_from_this());
    return session;
}

struct wasm_ql_rocksdb_plugin_impl {
    std::shared_ptr<rocksdb_database_interface> interface;
};

wasm_ql_rocksdb_plugin::wasm_ql_rocksdb_plugin()
    : my(std::make_shared<wasm_ql_rocksdb_plugin_impl>()) {}

wasm_ql_rocksdb_plugin::~wasm_ql_rocksdb_plugin() {}

void wasm_ql_rocksdb_plugin::set_program_options(options_description& cli, options_description& cfg) {}

void wasm_ql_rocksdb_plugin::plugin_initialize(const variables_map& options) {
    try {
        if (!my->interface) {
            my->interface               = std::make_shared<rocksdb_database_interface>();
            my->interface->rocksdb_inst = app().find_plugin<rocksdb_plugin>()->get_rocksdb_inst(true);
        }
        app().find_plugin<wasm_ql_plugin>()->set_database(my->interface);
    }
    FC_LOG_AND_RETHROW()
}

void wasm_ql_rocksdb_plugin::plugin_startup() {}
void wasm_ql_rocksdb_plugin::plugin_shutdown() { ilog("wasm_ql_rocksdb_plugin stopped"); }

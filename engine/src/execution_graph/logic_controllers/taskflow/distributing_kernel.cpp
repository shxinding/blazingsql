#include "distributing_kernel.h"
#include "utilities/CommonOperations.h"

namespace ral {
namespace cache {

distributing_kernel::distributing_kernel(std::size_t kernel_id,
        std::string expr,
        std::shared_ptr<Context> context,
        kernel_type kernel_type_id)
        : kernel{kernel_id, expr, context, kernel_type::DistributeAggregateKernel},
          node{ral::communication::CommunicationData::getInstance().getSelfNode()} {
}

void distributing_kernel::set_number_of_message_trackers(std::size_t num_message_trackers) {
    node_count.resize(num_message_trackers);
    messages_to_wait_for.resize(num_message_trackers);
}

void distributing_kernel::send_message(std::unique_ptr<ral::frame::BlazingTable> table,
        std::string specific_cache,
        std::string cache_id,
        std::string target_id,
        std::string total_rows,
        std::string message_id_prefix,
        bool always_add,
        bool wait_for,
        std::size_t message_tracker_idx,
        ral::cache::MetadataDictionary extra_metadata) {
    ral::cache::MetadataDictionary metadata;
    metadata.add_value(ral::cache::KERNEL_ID_METADATA_LABEL, std::to_string(kernel_id));
    metadata.add_value(ral::cache::QUERY_ID_METADATA_LABEL, std::to_string(context->getContextToken()));
    metadata.add_value(ral::cache::ADD_TO_SPECIFIC_CACHE_METADATA_LABEL, specific_cache);
    metadata.add_value(ral::cache::CACHE_ID_METADATA_LABEL, cache_id);
    metadata.add_value(ral::cache::SENDER_WORKER_ID_METADATA_LABEL, node.id());
    metadata.add_value(ral::cache::WORKER_IDS_METADATA_LABEL, target_id);

    if (total_rows!="") {
        metadata.add_value(ral::cache::TOTAL_TABLE_ROWS_METADATA_LABEL, total_rows);
    }

    const std::string MESSAGE_ID_CONTENT = metadata.get_values()[ral::cache::QUERY_ID_METADATA_LABEL] + "_" +
                                           metadata.get_values()[ral::cache::KERNEL_ID_METADATA_LABEL] + "_" +
                                           metadata.get_values()[ral::cache::SENDER_WORKER_ID_METADATA_LABEL];

    if (message_id_prefix!="") {
        metadata.add_value(
            ral::cache::MESSAGE_ID, message_id_prefix + MESSAGE_ID_CONTENT);
    } else {
        metadata.add_value(
            ral::cache::MESSAGE_ID, MESSAGE_ID_CONTENT);
    }

    for(auto meta_value : extra_metadata.get_values()) {
        metadata.add_value(meta_value.first, meta_value.second);
    }

    ral::cache::CacheMachine* output_cache = query_graph->get_output_message_cache();

    if(table==nullptr) {
        output_cache->addCacheData(std::make_unique<ral::cache::GPUCacheDataMetaData>(ral::utilities::create_empty_table({}, {}), metadata), "", always_add);
    } else {
        output_cache->addCacheData(std::unique_ptr<ral::cache::GPUCacheData>(new ral::cache::GPUCacheDataMetaData(std::move(table), metadata)), "", always_add);
    }

    if(wait_for) {
        messages_to_wait_for[message_tracker_idx].push_back(message_id_prefix + MESSAGE_ID_CONTENT);
    }

    if(specific_cache != "false") {
        node_count[message_tracker_idx][target_id]++;
    }
}

int distributing_kernel::get_total_partition_counts(std::size_t message_tracker_idx) {
    int total_count = node_count[message_tracker_idx][node.id()];
    for (auto message : messages_to_wait_for[message_tracker_idx]){
        auto meta_message = query_graph->get_input_message_cache()->pullCacheData(message);
        total_count += std::stoi(static_cast<ral::cache::GPUCacheDataMetaData *>(meta_message.get())->getMetadata().get_values()[ral::cache::PARTITION_COUNT]);
    }
    return total_count;
}

void distributing_kernel::send_total_partition_counts(
        std::string message_id_prefix,
        std::string cache_id,
        std::size_t message_tracker_idx) {
    auto nodes = context->getAllNodes();

    for(std::size_t i = 0; i < nodes.size(); ++i) {
        if(!(nodes[i] == node)) {
            ral::cache::MetadataDictionary extra_metadata;
            extra_metadata.add_value(ral::cache::PARTITION_COUNT, std::to_string(node_count[message_tracker_idx][nodes[i].id()]));

            send_message(nullptr,
                "false", //specific_cache
                cache_id, //cache_id
                nodes[i].id(), //target_id
                "", //total_rows
                message_id_prefix, //message_id_prefix
                true, //always_add
                true, //wait_for
                message_tracker_idx,
                extra_metadata);
        }
    }
}

void distributing_kernel::broadcast(ral::frame::BlazingTableView table_view,
        ral::cache::CacheMachine* output,
        std::string message_id_prefix,
        std::string cache_id,
        std::size_t message_tracker_idx) {
    auto nodes = context->getAllNodes();

    int self_node_idx = context->getNodeIndex(node);
    auto nodes_to_send = context->getAllOtherNodes(self_node_idx);
    std::string worker_ids_metadata;
    for (auto i = 0; i < nodes_to_send.size(); i++)	{
        if(nodes_to_send[i].id() != node.id()){
            worker_ids_metadata += nodes_to_send[i].id();
            if (i < nodes_to_send.size() - 1) {
                worker_ids_metadata += ",";
            }
        }
    }

    for(std::size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i] != node) {
            send_message(std::move(table_view.clone()),
                "true", //specific_cache
                cache_id, //cache_id
                worker_ids_metadata, //target_id
                "", //total_rows
                "", //message_id_prefix
                true //always_add
            );
        }
    }
}

void distributing_kernel::scatter(std::vector<ral::frame::BlazingTableView> partitions,
        ral::cache::CacheMachine* output,
        std::string message_id_prefix,
        std::string cache_id,
        std::size_t message_tracker_idx) {
    auto nodes = context->getAllNodes();
    assert(nodes.size() == partitions.size());

    for(std::size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i] == node) {
            // hash_partition followed by split does not create a partition that we can own, so we need to clone it.
            // if we dont clone it, hashed_data will go out of scope before we get to use the partition
            // also we need a BlazingTable to put into the cache, we cant cache views.
            output->addToCache(std::move(partitions[i].clone()), message_id_prefix, true);
            node_count[message_tracker_idx][node.id()]++;
        } else {
            send_message(std::move(partitions[i].clone()),
                "true", //specific_cache
                cache_id, //cache_id
                nodes[i].id(), //target_id
                "", //total_rows
                message_id_prefix, //message_id_prefix
                true, //always_add
                false, //wait_for
                message_tracker_idx //message_tracker_idx
            );
        }
    }
}

void distributing_kernel::scatterNodeColumnViews(std::vector<ral::distribution::NodeColumnView> partitions,
        ral::cache::CacheMachine* output,
        std::string message_id_prefix,
        std::size_t message_tracker_idx) {
    auto nodes = context->getAllNodes();

    std::vector<int32_t> part_ids(partitions.size());
    int num_partitions_per_node = partitions.size() / this->context->getTotalNodes();
    std::generate(part_ids.begin(), part_ids.end(), [count=0, num_partitions_per_node] () mutable { return (count++) % (num_partitions_per_node); });

    for (auto i = 0; i < partitions.size(); i++) {
        blazingdb::transport::Node dest_node;
        ral::frame::BlazingTableView table_view;
        std::tie(dest_node, table_view) = partitions[i];
        if(dest_node == node || table_view.num_rows() == 0) {
            continue;
        }

        send_message(std::move(table_view.clone()),
            "true", //specific_cache
            "output_" + std::to_string(part_ids[i]), //cache_id
            dest_node.id(), //target_id
            "", //total_rows
            message_id_prefix, //message_id_prefix
            true, //always_add
            false, //wait_for
            message_tracker_idx //message_tracker_idx
        );
    }

    for (auto i = 0; i < partitions.size(); i++) {
        auto & partition = partitions[i];
        if(partition.first == node) {
            std::string cache_id = "output_" + std::to_string(part_ids[i]);
            output->addToCache(std::move(partition.second.clone()), cache_id, true);
            node_count[message_tracker_idx][node.id()]++;
        }
    }
}

void distributing_kernel::increment_node_count(std::string node_id, std::size_t message_tracker_idx) {
    node_count[message_tracker_idx][node_id]++;
}

}  // namespace cache
}  // namespace ral

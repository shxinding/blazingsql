#pragma once

#include "protocols.hpp"
#include <vector>
#include <map>
#include <tuple>
#include <memory>
#include <rmm/device_buffer.hpp>
#include <transport/ColumnTransport.h>

#include "serializer.hpp"
#include "execution_graph/logic_controllers/CacheMachine.h"



namespace comm {

/**
 * A struct that lets us access the request that the end points ucx-py generates.
 */
struct ucx_request {
	//!!!!!!!!!!! do not modify this struct this has to match what is found in
	// https://github.com/rapidsai/ucx-py/blob/branch-0.15/ucp/_libs/ucx_api.pyx
	// Make sure to check on the latest branch !!!!!!!!!!!!!!!!!!!!!!!!!!!
	int completed; /**< Completion flag that we do not use. */
	int uid;	   /**< We store a map of request uid ==> buffer_transport to manage completion of send */
};


/**
 * A struct for managing the 64 bit tag that ucx uses
 * This allow us to make a value that is stored in 8 bytes
 */
struct blazing_ucp_tag {
	int message_id;			   /**< The message id which is generated by a global atomic*/
	uint16_t worker_origin_id; /**< The id to make sure each tag is unique */
	uint16_t frame_id;		   /**< The id of the frame being sent. 0 for being_transmission*/
};


  /**
  * @brief A Class used for the reconstruction of a BlazingTable from
  * metadata and column data
  */
class message_receiver {
using ColumnTransport = blazingdb::transport::ColumnTransport;

public:

  /**
  * @brief Constructs a message_receiver.
  *
  * This is a place for a message to receive chunks. It calls the deserializer after the complete
  * message has been assembled
  *
  * @param column_transports This is metadata about how a column will be reconstructed used by the deserialzer
  * @param metadata This is information about how the message was routed and payloads that are used in
  *                 execution, planning, or physical optimizations. E.G. num rows in table, num partitions to be processed
  * @param output_cache The destination for the message being received. It is either a specific cache inbetween
  *                     two kernels or it is intended for the general input cache using a mesage_id
  */
  message_receiver(const std::map<std::string, comm::node>& nodes, const std::vector<char> & buffer);
  virtual ~message_receiver(){

  }
  size_t buffer_size(u_int16_t index){
    return _buffer_sizes[index];
  }

  void allocate_buffer(uint16_t index, cudaStream_t stream = 0){
    if (index >= _raw_buffers.size()) {
      throw std::runtime_error("Invalid access to raw buffer");
    }
    _raw_buffers[index].resize(_buffer_sizes[index],stream);

  }

  node get_sender_node();

  size_t num_buffers(){
    return _buffer_sizes.size();
  }
  void confirm_transmission(){
    ++_buffer_counter;
    if (_buffer_counter == _raw_buffers.size()) {
      finish();
    }
  }

  void * get_buffer(uint16_t index){
    return _raw_buffers[index].data();
  }


  bool is_finished(){
    return (_buffer_counter == _raw_buffers.size());
  }

  void finish(cudaStream_t stream = 0) {
    std::unique_ptr<ral::frame::BlazingTable> table = deserialize_from_gpu_raw_buffers(_column_transports, _raw_buffers,stream);
    if ( _metadata.get_values()[ral::cache::ADD_TO_SPECIFIC_CACHE_METADATA_LABEL] == "true"){
      _output_cache->addToCache(std::move(table),  _metadata.get_values()[ral::cache::MESSAGE_ID], true);      
    } else {
      _output_cache->addCacheData(
              std::make_unique<ral::cache::GPUCacheDataMetaData>(std::move(table), _metadata), _metadata.get_values()[ral::cache::MESSAGE_ID], true);
    }
  }
private:


  std::vector<ColumnTransport> _column_transports;
  std::shared_ptr<ral::cache::CacheMachine> _output_cache;
  ral::cache::MetadataDictionary _metadata;
  std::vector<size_t> _buffer_sizes;
  std::vector<rmm::device_buffer> _raw_buffers;
  std::map<std::string, comm::node> _nodes_info_map;
  int _buffer_counter = 0;
};

} // namespace comm

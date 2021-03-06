// Author:  Bruce Allen <bdallen@nps.edu>
// Created: 2/25/2013
//
// The software provided here is released by the Naval Postgraduate
// School, an agency of the U.S. Department of Navy.  The software
// bears no warranty, either expressed or implied. NPS does not assume
// legal liability nor responsibility for a User's use of the software
// or the results of such use.
//
// Please note that within the United States, copyright protection,
// under Section 105 of the United States Code, Title 17, is not
// available for any work of the United States Government and/or for
// any works created by United States Government employees. User
// acknowledges that this software contains work which was created by
// NPS government employees and is therefore in the public domain and
// not subject to copyright.
//
// Released into the public domain on February 25, 2013 by Bruce Allen.

/**
 * \file
 * Generates MD5 hash values from hash_block_size data taken along sector
 * boundaries and scans for matches against a hash database.
 *
 * Note that the hash database may be accessed locally through the
 * file system or remotely through a socket.
 */

#include "bulk_extractor.h"

#ifdef HAVE_HASHID

#include "hashdb.hpp"
#include <dfxml/src/hash_t.h>

#include <iostream>
#include <unistd.h>	// for getpid
#include <sys/types.h>	// for getpid

// static values that can be set from config
static size_t hash_block_size = 4096;
static size_t sector_size = 512;
static hashdb::query_type_t query_type = hashdb::QUERY_NOT_SELECTED;
static std::string query_type_string = query_type_to_string(query_type);
std::string client_hashdb_path = "a valid hashdb directory path is required";
std::string client_socket_endpoint = "tcp://localhost:14500";

static bool scanner_is_usable = true;

// the hashdb query service
static hashdb::query_t* query = 0;

extern "C"
void scan_hashid(const class scanner_params &sp,
                 const recursion_control_block &rcb) {

    switch(sp.phase) {
        // startup
        case scanner_params::PHASE_STARTUP: {

            // set properties for this scanner
            sp.info->name        = "hashid";
            sp.info->author      = "Bruce Allen";
            sp.info->description = "Search hash IDs, specifically, search MD5 hashes against hashes in a MD5 hash database";

            // import query_type
            std::stringstream help_query_type;
            help_query_type  << "\n"
                             << "      <query_type> used to perform the query, where <query_type>\n"
                             << "      is one of use_path | use_socket (default "
                                                          << query_type_to_string(query_type) << ")\n"
                             << "      use_path   - Lookups are performed from a hashdb in the filesystem\n"
                             << "                   at the specified <path>.\n"
                             << "      use_socket - Lookups are performed from a server service at the\n"
                             << "                   specified <socket>.";
            sp.info->get_config("query_type", &query_type_string, help_query_type.str());

            // import path
            std::stringstream help_path;
            help_path        << "\n"
                             << "      Specifies the <path> to the hash database to be used for performing\n"
                             << "      the query service.  This option is only used when the query type\n"
                             << "      is set to \"use_path\".";
            sp.info->get_config("path", &client_hashdb_path, help_path.str());

            // import socket
            std::stringstream help_socket;
            help_socket      << "\n"
                             << "      Specifies the client <socket> endpoint to use to connect with the\n"
                             << "      hashdb_manager server (default '" << client_socket_endpoint << "').  Valid socket\n"
                             << "      transports supported by the zmq messaging kernel are tcp, ipc, and\n"
                             << "      inproc.  Currently, only tcp is tested.  This opition is only valid\n"
                             << "      when the query type is set to \"query_socket\".";
            sp.info->get_config("socket", &client_socket_endpoint, help_socket.str());

            // import hash_block_size
            sp.info->get_config("hash_block_size", &hash_block_size, "Hash block size, in bytes, used to generate hashes");

            // import sector_size
            std::stringstream help_sector_size;
            help_sector_size << "Sector size, in bytes\n";
            help_sector_size << "      Hashes are generated on each sector_size boundary.";
            sp.info->get_config("sector_size", &sector_size, help_sector_size.str());

            // configure the feature file if a usable query type is selected
            hashdb::query_type_t temp_query_type;
            bool temp_is_valid __attribute__ ((unused)) = string_to_query_type(query_type_string, temp_query_type);
            if (temp_query_type != hashdb::QUERY_NOT_SELECTED) {
                sp.info->feature_names.insert("identified_blocks");
            }

            return;
        }

        // init
        case scanner_params::PHASE_INIT: {

            // validate query_type
            bool is_valid = string_to_query_type(query_type_string, query_type);
            if (!is_valid) {
                std::cerr << "Error.  Value '" << query_type_string
                          << "' for parameter 'query_type' is invalid.\n"
                          << "Cannot continue.\n";
                exit(1);
            }

            // validate hash_block_size
            if (hash_block_size == 0) {
                std::cerr << "Error.  Value for parameter 'hash_block_size' is invalid.\n"
                         << "Cannot continue.\n";
                exit(1);
            }

            // validate sector_size
            if (sector_size == 0) {
                std::cerr << "Error.  Value for parameter 'sector_size' is invalid.\n"
                          << "Cannot continue.\n";
                exit(1);
            }

            // also, for valid operation, sectors must align on hash block boundaries
            if (hash_block_size % sector_size != 0) {
                std::cerr << "Error: invalid hash block size=" << hash_block_size
                          << " or sector size=" << sector_size << ".\n"
                          << "Sectors must align on hash block boundaries.\n"
                          << "Specifically, hash_block_size \% sector_size must be zero.\n"
                          << "Cannot continue.\n";
                exit(1);
            }

            // make sure the query service expects the same hash block size

            // TBD: call get_hashdb_info to get query service hash block size

            // it is bad if the expected hash block size is wrong
/* TBD
            if (success && response->hash_block_size != hash_block_size) {
                success = false;
                std::cerr << "Error: The scanner is hashing using a hash block size of " << hash_block_size << "\n"
                          << "but the hashdb contains hashes for data of hash block size " << response->hash_block_size << ".\n"
                          << "Cannot continue.\n";
            }
*/

            // perform setup based on selected query type
            std::string query_source;
            switch(query_type) {
                case hashdb::QUERY_USE_PATH:
                    query_source = client_hashdb_path;
                    break;
                case hashdb::QUERY_USE_SOCKET:
                    query_source = client_socket_endpoint;
                    break;
                default:
                    scanner_is_usable = false;
            }

            // open the query service
            if (scanner_is_usable) {
                query = new hashdb::query_t(query_type, query_source);
                int status = query->query_status();
                if (status != 0) {
                    // the requested query service failed to open
                    delete query;
                    scanner_is_usable = false;

                    std::cerr << "Query Error " << status << "\n"
                              << "The requested query service failed to open.\n"
                              << "Cannot continue.\n";
                    exit(1);
                }
            }
            return;
        }

        // scan
        case scanner_params::PHASE_SCAN: {
            if (!scanner_is_usable) {
                return;
            }

            // get the feature recorder
            feature_recorder* md5_recorder = sp.fs.get_name("identified_blocks");

            // get the sbuf
            const sbuf_t& sbuf = sp.sbuf;

            // allocate big space on heap for request and response
            hashdb::hashes_request_md5_t* request =
                                 new hashdb::hashes_request_md5_t;
            hashdb::hashes_response_md5_t* response =
                                 new hashdb::hashes_response_md5_t;

            // populate request with hashes calculated from hash blocks aligned on
            // sector boundaries from sbuf, which is the transaction block
            // use i as query id so that later it can be used as the feature
            // offset
            for (size_t i=0; i + hash_block_size <= sbuf.pagesize; i += sector_size) {
                // calculate the hash for this sector-aligned hash block
                md5_t md5 = md5_generator::hash_buf(sbuf.buf + i, hash_block_size);

                // convert md5 to uint8_t[]
                // note: could optimize and typecast instead.
                uint8_t digest[16];
                memcpy(digest, md5.digest, 16);

                // add the hash to the query hash request
                request->push_back(hashdb::hash_request_md5_t(i, digest));
            }

            // perform the query
            int status2 = query->query_hashes_md5(*request, *response);

            if (status2 == 0) {
                // record each feature in the response
                for (std::vector<hashdb::hash_response_md5_t>::const_iterator it = response->begin(); it != response->end(); ++it) {

                    // get the variables together for the feature
                    pos0_t pos0 = sbuf.pos0 + it->id;

                    // convert uint8_t[] to md5
                    md5_t md5;
                    memcpy(md5.digest,it->digest, 16);

                    std::string feature = md5.hexdigest();
                    stringstream ss;
                    ss << it->duplicates_count;
                    std::string context = ss.str();

                    // record the feature
                    md5_recorder->write(pos0, feature, context);
                }
            } else {
                // the query failed, likely a timeout from no server
                std::cerr << "Error in hashid hash query\n";
                exit(1);
            }

            // deallocate big space on heap for request and response
            delete request;
            delete response;

            return;
        }

        // shutdown
        case scanner_params::PHASE_SHUTDOWN: {
            if (!scanner_is_usable) {
                return;
            }

            // deallocate hashdb query service resources
            delete query;
            return;
        }

        // there are no other states
        default: {
            // no action for other states
            return;
        }
    }
}

#endif


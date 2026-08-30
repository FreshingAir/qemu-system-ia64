/*
 * ISP12160 A64 command and continuation IOCB parser
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/scsi/isp12160_iocb.h"
#include "qemu/bswap.h"

static bool bytes_are_zero(const uint8_t *bytes, size_t length)
{
    size_t i;

    for (i = 0; i < length; i++) {
        if (bytes[i]) {
            return false;
        }
    }
    return true;
}

static bool parse_segment(const uint8_t *entry, size_t offset,
                          ISP12160IOCBSegment *segment,
                          uint64_t *transfer_length, Error **errp)
{
    uint64_t address = ldq_le_p(entry + offset);
    uint32_t length = ldl_le_p(entry + offset + sizeof(address));

    if (!length) {
        error_setg(errp, "ISP12160 IOCB contains an empty data segment");
        return false;
    }
    if (address > UINT64_MAX - (length - 1U)) {
        error_setg(errp, "ISP12160 IOCB data segment wraps address space");
        return false;
    }
    if (*transfer_length > UINT64_MAX - length) {
        error_setg(errp, "ISP12160 IOCB transfer length overflows");
        return false;
    }

    segment->address = address;
    segment->length = length;
    *transfer_length += length;
    return true;
}

static bool unused_segments_are_zero(const uint8_t *entry, size_t first,
                                     size_t stride, size_t used, size_t total)
{
    size_t i;

    for (i = used; i < total; i++) {
        if (!bytes_are_zero(entry + first + i * stride, stride)) {
            return false;
        }
    }
    return true;
}

bool isp12160_iocb_parse_a64(const uint8_t *entries, size_t entry_count,
                             ISP12160IOCBCommand *command,
                             ISP12160IOCBSegment *segments,
                             size_t segment_capacity, Error **errp)
{
    const uint8_t *first;
    ISP12160IOCBCommand candidate = { 0 };
    g_autofree ISP12160IOCBSegment *candidate_segments = NULL;
    size_t expected_entries;
    size_t segment_index = 0;
    size_t continuation;
    uint16_t cdb_length;
    uint16_t direction_bits;
    uint8_t raw_target;

    if (!entries || !entry_count || entry_count > UINT8_MAX || !command ||
        (!segments && segment_capacity)) {
        error_setg(errp, "invalid ISP12160 IOCB parser input");
        return false;
    }

    first = entries;
    if (first[ISP12160_IOCB_HEADER_TYPE_OFFSET] !=
            ISP12160_IOCB_COMMAND_A64_TYPE ||
        first[ISP12160_IOCB_HEADER_COUNT_OFFSET] != entry_count ||
        first[ISP12160_IOCB_HEADER_STATUS_OFFSET]) {
        error_setg(errp, "invalid ISP12160 A64 command entry header");
        return false;
    }
    if (lduw_le_p(first + ISP12160_IOCB_A64_RESERVED_OFFSET) ||
        !bytes_are_zero(first + ISP12160_IOCB_A64_RESERVED1_OFFSET,
                        ISP12160_IOCB_A64_RESERVED1_BYTES)) {
        error_setg(errp, "ISP12160 A64 command reserved fields are nonzero");
        return false;
    }

    cdb_length = lduw_le_p(first + ISP12160_IOCB_A64_CDB_LENGTH_OFFSET);
    if (!cdb_length || cdb_length > ISP12160_IOCB_CDB_BYTES ||
        !bytes_are_zero(first + ISP12160_IOCB_A64_CDB_OFFSET + cdb_length,
                        ISP12160_IOCB_CDB_BYTES - cdb_length)) {
        error_setg(errp, "invalid ISP12160 A64 command CDB length or tail");
        return false;
    }
    candidate.cdb_length = cdb_length;

    candidate.control_flags = lduw_le_p(
        first + ISP12160_IOCB_A64_CONTROL_FLAGS_OFFSET);
    if (candidate.control_flags &
        ~(ISP12160_IOCB_CONTROL_SIMPLE_TAG |
          ISP12160_IOCB_CONTROL_DATA_FROM_DEVICE |
          ISP12160_IOCB_CONTROL_DATA_TO_DEVICE)) {
        error_setg(errp, "unsupported ISP12160 A64 command control flags");
        return false;
    }
    direction_bits = candidate.control_flags &
        (ISP12160_IOCB_CONTROL_DATA_FROM_DEVICE |
         ISP12160_IOCB_CONTROL_DATA_TO_DEVICE);
    switch (direction_bits) {
    case 0:
        candidate.direction = ISP12160_IOCB_DIRECTION_NONE;
        break;
    case ISP12160_IOCB_CONTROL_DATA_FROM_DEVICE:
        candidate.direction = ISP12160_IOCB_DIRECTION_FROM_DEVICE;
        break;
    case ISP12160_IOCB_CONTROL_DATA_TO_DEVICE:
        candidate.direction = ISP12160_IOCB_DIRECTION_TO_DEVICE;
        break;
    default:
        error_setg(errp, "bidirectional ISP12160 IOCBs are unsupported");
        return false;
    }

    candidate.segment_count = lduw_le_p(
        first + ISP12160_IOCB_A64_SEGMENT_COUNT_OFFSET);
    expected_entries = 1;
    if (candidate.segment_count > ISP12160_IOCB_COMMAND_A64_SEGMENTS) {
        expected_entries += DIV_ROUND_UP(
            candidate.segment_count - ISP12160_IOCB_COMMAND_A64_SEGMENTS,
            ISP12160_IOCB_CONTINUE_A64_SEGMENTS);
    }
    if (entry_count != expected_entries) {
        error_setg(errp,
                   "ISP12160 A64 command entry count does not match segments");
        return false;
    }
    if (candidate.segment_count > segment_capacity) {
        error_setg(errp, "ISP12160 A64 command exceeds segment capacity");
        return false;
    }
    if ((candidate.segment_count == 0) !=
        (candidate.direction == ISP12160_IOCB_DIRECTION_NONE)) {
        error_setg(errp,
                   "ISP12160 A64 command direction disagrees with segments");
        return false;
    }

    if (candidate.segment_count) {
        candidate_segments = g_new0(ISP12160IOCBSegment,
                                    candidate.segment_count);
    }
    while (segment_index < candidate.segment_count &&
           segment_index < ISP12160_IOCB_COMMAND_A64_SEGMENTS) {
        if (!parse_segment(first,
                           ISP12160_IOCB_A64_SEGMENT0_OFFSET +
                           segment_index *
                               ISP12160_IOCB_A64_SEGMENT_STRIDE,
                           &candidate_segments[segment_index],
                           &candidate.transfer_length, errp)) {
            return false;
        }
        segment_index++;
    }
    if (!unused_segments_are_zero(
            first, ISP12160_IOCB_A64_SEGMENT0_OFFSET,
            ISP12160_IOCB_A64_SEGMENT_STRIDE,
            MIN((size_t)candidate.segment_count,
                (size_t)ISP12160_IOCB_COMMAND_A64_SEGMENTS),
            ISP12160_IOCB_COMMAND_A64_SEGMENTS)) {
        error_setg(errp, "ISP12160 A64 command has an unused data segment");
        return false;
    }

    for (continuation = 1; continuation < entry_count; continuation++) {
        const uint8_t *entry = entries +
            continuation * ISP12160_IOCB_ENTRY_BYTES;
        size_t used = MIN((size_t)ISP12160_IOCB_CONTINUE_A64_SEGMENTS,
                          candidate.segment_count - segment_index);
        size_t i;

        if (entry[ISP12160_IOCB_HEADER_TYPE_OFFSET] !=
                ISP12160_IOCB_CONTINUE_A64_TYPE ||
            entry[ISP12160_IOCB_HEADER_COUNT_OFFSET] != 1 ||
            entry[ISP12160_IOCB_HEADER_STATUS_OFFSET]) {
            error_setg(errp,
                       "invalid ISP12160 A64 continuation entry header");
            return false;
        }
        for (i = 0; i < used; i++, segment_index++) {
            if (!parse_segment(entry,
                               ISP12160_IOCB_CONT_SEGMENT0_OFFSET +
                               i * ISP12160_IOCB_CONT_SEGMENT_STRIDE,
                               &candidate_segments[segment_index],
                               &candidate.transfer_length, errp)) {
                return false;
            }
        }
        if (!unused_segments_are_zero(
                entry, ISP12160_IOCB_CONT_SEGMENT0_OFFSET,
                ISP12160_IOCB_CONT_SEGMENT_STRIDE, used,
                ISP12160_IOCB_CONTINUE_A64_SEGMENTS)) {
            error_setg(errp,
                       "ISP12160 A64 continuation has an unused segment");
            return false;
        }
    }

    candidate.entry_count = entry_count;
    candidate.handle = ldl_le_p(first + ISP12160_IOCB_A64_HANDLE_OFFSET);
    candidate.timeout = lduw_le_p(
        first + ISP12160_IOCB_A64_TIMEOUT_OFFSET);
    candidate.lun = first[ISP12160_IOCB_A64_LUN_OFFSET];
    raw_target = first[ISP12160_IOCB_A64_TARGET_OFFSET];
    candidate.channel = raw_target >> 7;
    candidate.target = raw_target & 0x7f;
    if (candidate.lun >= 8 || candidate.target >= 16) {
        error_setg(errp, "invalid ISP12160 A64 command target or LUN");
        return false;
    }
    memcpy(candidate.cdb, first + ISP12160_IOCB_A64_CDB_OFFSET,
           candidate.cdb_length);

    *command = candidate;
    if (candidate.segment_count) {
        memcpy(segments, candidate_segments,
               candidate.segment_count * sizeof(*segments));
    }
    return true;
}

static bool completion_status_valid(uint16_t status)
{
    switch (status) {
    case ISP12160_IOCB_CS_COMPLETE:
    case ISP12160_IOCB_CS_INCOMPLETE:
    case ISP12160_IOCB_CS_DMA_ERROR:
    case ISP12160_IOCB_CS_TRANSPORT_ERROR:
    case ISP12160_IOCB_CS_RESET:
    case ISP12160_IOCB_CS_ABORTED:
    case ISP12160_IOCB_CS_TIMEOUT:
    case ISP12160_IOCB_CS_DATA_OVERRUN:
    case ISP12160_IOCB_CS_DATA_UNDERRUN:
    case ISP12160_IOCB_CS_INVALID_ENTRY_TYPE:
        return true;
    default:
        return false;
    }
}

bool isp12160_iocb_build_status(uint8_t *entry, size_t entry_size,
                                const ISP12160IOCBStatus *status,
                                Error **errp)
{
    uint8_t candidate[ISP12160_IOCB_ENTRY_BYTES] = { 0 };

    if (!entry || entry_size != sizeof(candidate) || !status) {
        error_setg(errp, "invalid ISP12160 status IOCB builder input");
        return false;
    }
    if (status->scsi_status > UINT8_MAX ||
        !completion_status_valid(status->completion_status) ||
        status->state_flags & ~ISP12160_IOCB_STATE_FLAGS_MASK ||
        status->sense_length > ISP12160_IOCB_SENSE_BYTES) {
        error_setg(errp, "invalid ISP12160 status IOCB fields");
        return false;
    }

    candidate[ISP12160_IOCB_HEADER_TYPE_OFFSET] = ISP12160_IOCB_STATUS_TYPE;
    candidate[ISP12160_IOCB_HEADER_COUNT_OFFSET] = 1;
    stl_le_p(candidate + ISP12160_IOCB_STATUS_HANDLE_OFFSET,
             status->handle);
    stw_le_p(candidate + ISP12160_IOCB_STATUS_SCSI_STATUS_OFFSET,
             status->scsi_status);
    stw_le_p(candidate + ISP12160_IOCB_STATUS_COMPLETION_OFFSET,
             status->completion_status);
    stw_le_p(candidate + ISP12160_IOCB_STATUS_STATE_FLAGS_OFFSET,
             status->state_flags);
    stw_le_p(candidate + ISP12160_IOCB_STATUS_STATUS_FLAGS_OFFSET,
             status->status_flags);
    stw_le_p(candidate + ISP12160_IOCB_STATUS_TIME_OFFSET,
             status->time);
    stw_le_p(candidate + ISP12160_IOCB_STATUS_SENSE_LENGTH_OFFSET,
             status->sense_length);
    stl_le_p(candidate + ISP12160_IOCB_STATUS_RESIDUAL_OFFSET,
             status->residual_length);
    memcpy(candidate + ISP12160_IOCB_STATUS_SENSE_OFFSET, status->sense,
           status->sense_length);

    memcpy(entry, candidate, sizeof(candidate));
    return true;
}

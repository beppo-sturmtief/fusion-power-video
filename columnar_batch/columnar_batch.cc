#include "columnar_batch.h"

namespace fpvc {

    BatchSchema::BatchSchema(size_t xsize, size_t ysize, size_t shifted_left, Frame &uncompressed_delta_frame) :
        xsize_(xsize), ysize_(ysize), shifted_left_(shifted_left), 
        compressed_delta_frame_high_plane_(Frame::MaxCompressedPlaneSize(xsize, ysize)),
        compressed_delta_frame_low_plane_(Frame::MaxCompressedPlaneSize(xsize, ysize)) {
        
        size_t encoded_high_size = compressed_delta_frame_high_plane_.size();
        size_t encoded_low_size = compressed_delta_frame_low_plane_.size();
        size_t encoded_preview_size = 0;

        uncompressed_delta_frame.CompressPredicted(&encoded_high_size, compressed_delta_frame_high_plane_.data(), 
                &encoded_low_size, compressed_delta_frame_low_plane_.data(),
                &encoded_preview_size, nullptr);
        compressed_delta_frame_high_plane_.resize(encoded_high_size);
        compressed_delta_frame_low_plane_.resize(encoded_low_size);
    }

    Batch::Batch(size_t batch_size, SchemaPtr schema) :
                schema_(schema), batch_size_(batch_size), length_(0), 
                previews_capacity_((Frame::MaxCompressedPreviewSize(schema->xsize(), schema->ysize()) + 63) & 0x7ffffffc0) ,
                high_or_low_capacity_((Frame::MaxCompressedPlaneSize(schema->xsize(), schema->ysize()) + 63) & 0x7ffffffc0) {
        // round up to 64 byte boundaries
        size_t timestamps_capacity = (batch_size_ * sizeof(int64_t) + 63) & 0x7ffffffc0;
        size_t flags_capacity = (batch_size_ * sizeof(uint8_t) + 63) & 0x7ffffffc0;
        size_t offsets_capacity = ((1 + batch_size_) * sizeof(uint32_t) + 63) & 0x7ffffffc0;

        backing_buffer_ = std::vector<uint8_t>(timestamps_capacity + flags_capacity + 3 * offsets_capacity + previews_capacity_ + 2 * high_or_low_capacity_);

        timestamps_ = reinterpret_cast<int64_t*>(backing_buffer_.data());
        flags_ = backing_buffer_.data() + timestamps_capacity;

        preview_offsets_ = reinterpret_cast<uint32_t*>(backing_buffer_.data() + timestamps_capacity + flags_capacity);
        high_plane_offsets_ = reinterpret_cast<uint32_t*>(backing_buffer_.data() + timestamps_capacity + flags_capacity + offsets_capacity);
        low_plane_offsets_ = reinterpret_cast<uint32_t*>(backing_buffer_.data() + timestamps_capacity + flags_capacity + 2 * offsets_capacity);

        preview_ = backing_buffer_.data() + timestamps_capacity + flags_capacity + 3 * offsets_capacity;
        high_plane_ = backing_buffer_.data() + timestamps_capacity + flags_capacity + 3 * offsets_capacity + previews_capacity_;
        low_plane_ = backing_buffer_.data() + timestamps_capacity + flags_capacity + 3 * offsets_capacity + previews_capacity_ + high_or_low_capacity_;

        preview_offsets_[0] = preview_offsets_[1] = 0;
        high_plane_offsets_[0] = high_plane_offsets_[1] = 0;
        low_plane_offsets_[0] = low_plane_offsets_[1] = 0;
    }

    void Batch::Reset() {
        length_ = 0;
        preview_offsets_[0] = preview_offsets_[1] = 0;
        high_plane_offsets_[0] = high_plane_offsets_[1] = 0;
        low_plane_offsets_[0] = low_plane_offsets_[1] = 0;
    }

    bool Batch::AppendPredicted(Frame predicted_frame) {
        if (length_ >= batch_size_) {
            return false;
        }

        timestamps_[length_] = predicted_frame.timestamp();
        flags_[length_] = predicted_frame.flags();

        size_t encoded_high_size = high_or_low_capacity_ - high_plane_offsets_[length_];
        size_t encoded_low_size = high_or_low_capacity_ - low_plane_offsets_[length_];
        size_t encoded_preview_size = previews_capacity_ - preview_offsets_[length_];

        predicted_frame.CompressPredicted(&encoded_high_size, high_plane_ + high_plane_offsets_[length_], 
                &encoded_low_size, low_plane_ + low_plane_offsets_[length_],
                &encoded_preview_size, preview_ + preview_offsets_[length_]);
        
        high_plane_offsets_[length_ + 1] = high_plane_offsets_[length_] + encoded_high_size;
        low_plane_offsets_[length_ + 1] = low_plane_offsets_[length_] + encoded_low_size;
        preview_offsets_[length_ + 1] = preview_offsets_[length_] + encoded_preview_size;
        length_++;

        return true;
    }

}
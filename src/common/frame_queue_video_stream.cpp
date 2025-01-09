#include "frame_queue_video_stream.hpp"

namespace godot {

void FrameQueueVideoStream::update_texture_from_frame(const VideoFrame& frame) {
    PackedByteArray pba;
    pba.resize(frame.data.size());
    memcpy(pba.ptrw(), frame.data.data(), frame.data.size());
    
    Ref<Image> img = Image::create_from_data(
        frame.size.x, frame.size.y,
        false, Image::FORMAT_RGBA8,
        pba
    );
    
    if (img.is_valid()) {
        if (texture->get_size() == frame.size) {
            texture->update(img);
        } else {
            texture->set_image(img);
        }
    }
}

void FrameQueueVideoStream::setup_dimensions(size_t width, size_t height) {
    dimensions.frame.x = width;
    dimensions.frame.y = height;
    dimensions.aligned_width = align_dimension(width);
    dimensions.aligned_height = align_dimension(height);
}

size_t FrameQueueVideoStream::align_dimension(size_t dim, size_t alignment) {
    return (dim + alignment - 1) & ~(alignment - 1);
}

double FrameQueueVideoStream::predict_next_frame_time(double current_time, float fps) {
    return current_time + (1.0 / fps);
}

void FrameQueueVideoStream::_update(double delta) {
    if (!state.playing || state.paused) {
        return;
    }

    state.engine_time += delta;
    update_frame_queue(delta);
    
    const std::optional<VideoFrame> frame = frame_queue.try_pop_next_frame(state.engine_time);
    if (frame) {
        update_texture_from_frame(*frame);
    }

    if (check_end_of_stream()) {
        state.playing = false;
        state.engine_time = 0.0;
        frame_queue.clear();
    }
}

} // namespace godot

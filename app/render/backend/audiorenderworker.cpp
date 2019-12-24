#include "audiorenderworker.h"

#include "audio/audiomanager.h"

AudioRenderWorker::AudioRenderWorker(QObject *parent) :
  RenderWorker(parent)
{
}

void AudioRenderWorker::SetParameters(const AudioRenderingParams &audio_params)
{
  audio_params_ = audio_params;
}

bool AudioRenderWorker::InitInternal()
{
  // Nothing to init yet
  return true;
}

void AudioRenderWorker::CloseInternal()
{
  // Nothing to init yet
}

FramePtr AudioRenderWorker::RetrieveFromDecoder(DecoderPtr decoder, const TimeRange &range)
{
  return decoder->RetrieveAudio(range.in(), range.out() - range.in(), audio_params_);
}

NodeValueTable AudioRenderWorker::RenderBlock(const TrackOutput *track, const TimeRange &range)
{
  QList<Block*> active_blocks = track->BlocksAtTimeRange(range);

  // All these blocks will need to output to a buffer so we create one here
  QByteArray block_range_buffer(audio_params_.time_to_bytes(range.length()), 0);

  NodeValueTable merged_table;

  // Loop through active blocks retrieving their audio
  foreach (Block* b, active_blocks) {
    TimeRange range_for_block(qMax(b->in(), range.in()),
                              qMin(b->out(), range.out()));

    NodeValueTable table = ProcessNode(NodeDependency(b,
                                                      range_for_block));

    QByteArray samples_from_this_block = table.Take(NodeParam::kSamples).toByteArray();
    int destination_offset = audio_params_.time_to_bytes(range_for_block.in() - range.in());
    int maximum_copy_size = audio_params_.time_to_bytes(range_for_block.length());
    int copied_size = 0;

    if (!samples_from_this_block.isEmpty()) {
      // Stretch samples here
      if (b->media_length() != b->length()) {
        QByteArray speed_adjusted_samples;

        double clip_speed = b->speed();

        int sample_count = audio_params_.bytes_to_samples(samples_from_this_block.size());

        for (double i=0;i<sample_count;i+=clip_speed) {
          int sample_index = qFloor(i);
          int byte_index = audio_params_.samples_to_bytes(sample_index);

          QByteArray sample_at_this_index = samples_from_this_block.mid(byte_index, audio_params_.samples_to_bytes(1));
          speed_adjusted_samples.append(sample_at_this_index);
        }

        samples_from_this_block = speed_adjusted_samples;
      }

      if (b->is_reversed()) {
        int sample_size = audio_params_.samples_to_bytes(1);
        int half_buffer_sz = samples_from_this_block.size() / 2;
        char* temp_buffer = new char[sample_size];

        for (int src_index=0;src_index<half_buffer_sz;src_index+=sample_size) {
          char* src_ptr = samples_from_this_block.data() + src_index;
          char* dst_ptr = samples_from_this_block.data() + samples_from_this_block.size() - sample_size - src_index;

          // Simple swap
          memcpy(temp_buffer, src_ptr, static_cast<size_t>(sample_size));
          memcpy(src_ptr, dst_ptr, static_cast<size_t>(sample_size));
          memcpy(dst_ptr, temp_buffer, static_cast<size_t>(sample_size));
        }

        delete [] temp_buffer;
      }

      copied_size = samples_from_this_block.size();

      memcpy(block_range_buffer.data()+destination_offset,
             samples_from_this_block.data(),
             static_cast<size_t>(copied_size));
    }

    if (copied_size < maximum_copy_size) {
      memset(block_range_buffer.data()+destination_offset+copied_size,
             0,
             static_cast<size_t>(maximum_copy_size - copied_size));
    }

    NodeValueTable::Merge({merged_table, table});
  }

  merged_table.Push(NodeParam::kSamples, block_range_buffer);

  return merged_table;
}

const AudioRenderingParams &AudioRenderWorker::audio_params() const
{
  return audio_params_;
}

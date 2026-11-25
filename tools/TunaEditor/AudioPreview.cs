using System;
using System.IO;
using NAudio.Wave;
using NAudio.Vorbis;

namespace TunaEditor
{
    /// <summary>
    /// Plays an audio file for the Quick Play action, without involving the engine.
    ///
    /// This used MCI, which cannot decode Vorbis -- Windows ships no .ogg device -- leaving 27
    /// of this project's clips unplayable. NAudio handles .wav and .mp3, and NAudio.Vorbis adds
    /// .ogg, so all 398 play. Output is streamed rather than decoded up front, so starting a
    /// 4 MB .ogg does not first expand it to ~40 MB of PCM.
    /// </summary>
    internal static class AudioPreview
    {
        private static IWavePlayer output;
        private static WaveStream reader;

        /// <summary>Plays the file, replacing anything already playing. Returns null on success.</summary>
        public static string Play(string path)
        {
            Stop();

            string extension = Path.GetExtension(path).ToLowerInvariant();

            try
            {
                reader = OpenReader(path, extension);
                if (reader == null)
                {
                    return "Quick Play does not handle " + extension + " files.";
                }

                output = new WaveOutEvent();
                output.PlaybackStopped += OnPlaybackStopped;
                output.Init(reader);
                output.Play();
                return null;
            }
            catch (Exception ex)
            {
                Stop();
                return ex.Message;
            }
        }

        private static WaveStream OpenReader(string path, string extension)
        {
            switch (extension)
            {
                case ".ogg":
                    return new VorbisWaveReader(path);
                case ".wav":
                case ".mp3":
                    return new AudioFileReader(path);
                default:
                    return null;
            }
        }

        /// <summary>Duration of the clip, or null when nothing is loaded.</summary>
        public static TimeSpan? Duration
        {
            get { return reader != null ? reader.TotalTime : (TimeSpan?)null; }
        }

        private static void OnPlaybackStopped(object sender, StoppedEventArgs e)
        {
            // Release the device as soon as the clip ends, rather than holding it until the
            // next Quick Play.
            Stop();
        }

        public static void Stop()
        {
            if (output != null)
            {
                output.PlaybackStopped -= OnPlaybackStopped;
                try { output.Stop(); } catch (Exception) { }
                try { output.Dispose(); } catch (Exception) { }
                output = null;
            }

            if (reader != null)
            {
                try { reader.Dispose(); } catch (Exception) { }
                reader = null;
            }
        }
    }
}

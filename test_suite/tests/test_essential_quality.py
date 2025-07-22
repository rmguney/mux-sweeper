#!/usr/bin/env python3
"""
Essential Quality Tests

Core quality tests that detect the most critical issues:
1. Basic recording functionality
2. Audio/Video sync issues
3. File validity checks
"""

import subprocess
import time
from pathlib import Path


class TestEssentialQuality:
    """Essential quality tests for core functionality."""
    
    def setup_method(self):
        """Setup test environment."""
        self.project_root = Path(__file__).parent.parent.parent
        self.muxsw_exe = self.project_root / "release" / "muxsw.exe"
        self.test_outputs = self.project_root / "test_suite" / "outputs"
        self.test_outputs.mkdir(parents=True, exist_ok=True)
        
        # Clean up old test files
        for old_file in self.test_outputs.glob("quality_test_*.mp4"):
            old_file.unlink(missing_ok=True)
            
    def _record_test_file(self, args, output_file, duration):
        """Record a test file with given arguments."""
        try:
            cmd = [str(self.muxsw_exe)] + args + ["--out", str(output_file), "--time", str(duration)]
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=duration + 10, check=False)
            
            if result.returncode != 0:
                print(f"[ERROR] Recording failed: {result.stderr}")
                return False
                
            if not output_file.exists():
                print(f"[ERROR] Output file not created: {output_file}")
                return False
                
            if output_file.stat().st_size < 1000:
                print(f"[ERROR] Output file too small: {output_file.stat().st_size} bytes")
                return False
                
            return True
            
        except subprocess.TimeoutExpired:
            print(f"[ERROR] Recording timed out after {duration + 10}s")
            return False
        except Exception as e:
            print(f"[ERROR] Recording failed: {e}")
            return False
    
    def _get_stream_duration(self, file_path, stream_type):
        """Get duration of specific stream (v:0 for video, a:0 for audio)."""
        try:
            result = subprocess.run([
                'ffprobe', '-v', 'quiet', 
                '-show_entries', 'stream=duration',
                '-select_streams', stream_type,
                '-of', 'csv=p=0',
                str(file_path)
            ], capture_output=True, text=True, timeout=10)
            
            if result.returncode == 0 and result.stdout.strip():
                return float(result.stdout.strip())
            return None
        except Exception:
            return None
    
    def test_basic_video_recording(self):
        """Test basic video recording functionality."""
        print("Testing basic video recording...")
        
        output_file = self.test_outputs / "quality_test_video_only.mp4"
        success = self._record_test_file(["--video"], output_file, 3)
        
        assert success, "Basic video recording failed"
        
        # Check file can be analyzed
        video_duration = self._get_stream_duration(output_file, "v:0")
        assert video_duration is not None, "Video stream not found or invalid"
        assert 2.5 <= video_duration <= 3.5, f"Video duration {video_duration}s not close to expected 3s"
        
        print(f"[PASS] Basic video recording: {video_duration:.2f}s")
        
    def test_mixed_recording_sync(self):
        """Test that mixed audio/video recordings have synchronized streams."""
        print("Testing audio/video sync...")
        
        output_file = self.test_outputs / "quality_test_mixed.mp4"
        success = self._record_test_file(["--video", "--system"], output_file, 3)
        
        assert success, "Mixed recording failed"
        
        # Check both streams exist and have similar durations
        video_duration = self._get_stream_duration(output_file, "v:0")
        audio_duration = self._get_stream_duration(output_file, "a:0")
        
        assert video_duration is not None, "Video stream not found"
        assert audio_duration is not None, "Audio stream not found"
        
        duration_diff = abs(video_duration - audio_duration)
        assert duration_diff <= 0.2, f"Audio/Video sync issue: Video={video_duration:.3f}s, Audio={audio_duration:.3f}s, Diff={duration_diff:.3f}s"
        
        print(f"[PASS] A/V sync check: Video={video_duration:.2f}s, Audio={audio_duration:.2f}s")
        
    def test_file_playability(self):
        """Test that generated files are playable."""
        print("Testing file playability...")
        
        output_file = self.test_outputs / "quality_test_playable.mp4"
        success = self._record_test_file(["--video"], output_file, 2)
        
        assert success, "Recording for playability test failed"
        
        # Use ffprobe to verify file structure
        try:
            result = subprocess.run([
                'ffprobe', '-v', 'quiet',
                '-show_format', '-show_streams',
                str(output_file)
            ], capture_output=True, text=True, timeout=10)
            
            assert result.returncode == 0, f"File not readable by ffprobe: {result.stderr}"
            
            output = result.stdout
            assert 'codec_name' in output, "No valid codec information found"
            assert 'duration' in output, "No duration information found"
            
            print("[PASS] File playability check")
            
        except Exception as e:
            assert False, f"Playability test failed: {e}"

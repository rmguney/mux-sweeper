#!/usr/bin/env python3
"""
Create a simple test file to generate audio during system audio capture testing.
"""

import time
import subprocess
import sys
from pathlib import Path

def generate_test_audio(duration_seconds=5):
    """Generate a simple test tone using ffmpeg for system audio testing."""
    # Use ffmpeg to generate a sine wave tone to the default audio output
    cmd = [
        'ffplay', 
        '-f', 'lavfi', 
        '-i', f'sine=frequency=440:duration={duration_seconds}',
        '-nodisp',  # No display
        '-autoexit'  # Exit when finished
    ]
    
    print(f"Generating {duration_seconds}s test tone (440 Hz sine wave)...")
    print("This will help test system audio capture with actual audio data.")
    
    try:
        subprocess.run(cmd, check=True)
        print("Test tone generation completed.")
    except subprocess.CalledProcessError as e:
        print(f"Error generating test tone: {e}")
        return False
    except FileNotFoundError:
        print("ffplay not found. Installing tone generator alternative...")
        # Fallback: use PowerShell to generate a beep
        ps_cmd = '''
        Add-Type -AssemblyName System.Windows.Forms
        [System.Windows.Forms.SystemSounds]::Beep.Play()
        Start-Sleep -Seconds ''' + str(duration_seconds)
        
        subprocess.run(['powershell', '-Command', ps_cmd])
    
    return True

if __name__ == "__main__":
    duration = int(sys.argv[1]) if len(sys.argv) > 1 else 3
    generate_test_audio(duration)

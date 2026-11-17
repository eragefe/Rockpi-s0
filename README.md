arecord -N -D hw:0,0 -f S16_LE -c 2 -r 44100 -t raw >> /tmp/spdif-live.raw | tail -f -c +1 /tmp/spdif-live.raw | aplay -D hw:1,0 -f S16_LE -c 2 -r 44100 -t raw

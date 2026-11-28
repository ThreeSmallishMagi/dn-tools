convert -size 512x512 gradient: -fx "sin(2*pi*i/8)*cos(2*pi*j/8)*0.5+0.5" sine_wave8.png
convert -size 513x513 gradient: -fx "sin(2*pi*i/9)*cos(2*pi*j/9)*0.5+0.5" sine_wave9.png
convert -size 520x520 gradient: -fx "sin(2*pi*i/10)*cos(2*pi*j/10)*0.5+0.5" sine_wave10.png
convert -size 512x512 gradient: -fx "sin(2*pi*i/16)*cos(2*pi*j/16)*0.5+0.5" sine_wave16.png
convert -size 256x256 gradient:"gray(96)-gray(128)" -transpose ramp32.png
convert -size 256x256 pattern:horizontal2 lines.png
convert -size 512x512 xc:gray50 grey.png
dither grey.png --levels 8 --out greyd.png

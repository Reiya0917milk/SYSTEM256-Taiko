set -e
SDCC="/c/Program Files/SDCC/bin/sdcc.exe"
SDOBJCOPY="/c/Program Files/SDCC/bin/sdobjcopy.exe"
CFLAGS="-V -mmcs51 --model-large --xram-size 0x1800 --xram-loc 0x0000 --code-size 0xec00 --stack-auto --opt-code-speed -I../chlib/src -I../jvsio"

echo "=== us/ sources ==="
for f in main client controller settings soft485; do
  echo "-- $f.c --"
  "$SDCC" -c $CFLAGS -I../ -o build/$f.rel $f.c
done

echo "=== chlib/src/ sources ==="
for f in adc ch559 gpio led pwm1 serial timer3; do
  echo "-- $f.c --"
  "$SDCC" -c $CFLAGS -o build/$f.rel ../chlib/src/$f.c
done

echo "=== chlib/src/usb/ sources ==="
"$SDCC" -c $CFLAGS -o build/usb_host.rel ../chlib/src/usb/usb_host.c

echo "=== chlib/src/usb/hid/ sources ==="
for f in hid hid_dualshock3 hid_guncon3 hid_keyboard hid_mouse hid_switch hid_xbox; do
  echo "-- $f.c --"
  "$SDCC" -c $CFLAGS -o build/$f.rel ../chlib/src/usb/hid/$f.c
done

echo "=== jvsio/ sources ==="
"$SDCC" -c $CFLAGS -I ../chlib/src -o build/jvsio_node.rel ../jvsio/jvsio_node.c

echo "=== LINK ==="
OBJS="main client controller settings soft485 adc ch559 gpio hid hid_dualshock3 hid_guncon3 hid_keyboard hid_mouse hid_switch hid_xbox led pwm1 serial timer3 usb_host jvsio_node"
OBJPATHS=""
for f in $OBJS; do OBJPATHS="$OBJPATHS build/$f.rel"; done
"$SDCC" $CFLAGS $OBJPATHS -o build/iona.ihx

echo "=== BIN CONVERT ==="
"$SDOBJCOPY" -I ihex -O binary build/iona.ihx iona.bin

echo "=== DONE ==="
ls -la iona.bin

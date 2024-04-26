# Proxmark 3 NXTGEN GUI

The Proxmark 3 client uses QT5 (with QT4 as a fallback) as it's framework of choice to display information to it's users vis the plotting window. Certian configurations, like compiling for Termix support, doesn't use this GUI, so it is compiled with:

```sh
make SKIPQT=1
```

These files should only contain code needed to run the GUI and reference things from outside itself to do that job. There should be no code that references these files unless absolutely necessary, and with the correct precautions taken to handle if the GUI isn't enabled.
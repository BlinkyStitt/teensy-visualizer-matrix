# Teensy Visualizer Matrix

I forked [my code for a single strip of LEDs/8 EL wires](https://github.com/WyseNynja/teensy-visualizer) to instetad run 2 64x8 LED matrixes. Now that that works, I'm merging these repositories back together. I think the EL wire code is working, but I haven't tested it yett.

## TODO

* bring back support for the hat with a single strip of LEDs
* merge this repo into <https://github.com/WyseNynja/teensy-visualizer>
* parts list
* document soldering things and putting the hat together
* more/better code comments
* color blind pallete instead of rainbow hues
* scroll a message when dbspl (sound pressure level) at dangerous levels
* A lot of the functions mutate globals. This works, but I'd like to pass function arguments around instead so that I can write tests.
* sprites
* document running Arduino IDE with verbose compilation output to get all the right flags for .vscode/c_cpp_properties.json
* document installing <https://github.com/WyseNynja/bs_debug>
* use less floats

## Reading

* <https://github.com/AaronLiddiment/LEDMatrix/wiki/1.Setting-Up>
* <https://forum.pjrc.com/threads/42958-Question-about-the-spectrum-analyzer-example-best-practises-and-making-it-smooth>
* <https://plot.ly/~mrlyule/16/equal-loudness-contours-iso-226-2003/#/>
* <https://pythonhosted.org/pydsm/intro.html>
* <https://forum.pjrc.com/threads/33390-FFT-convert-real-values-to-decibles>

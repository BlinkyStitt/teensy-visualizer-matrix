# Ideas

## Aaron

when its falling, turn the color off and only do white? or maybe half of them

cycle color pallets

change speed

scale frequency bins to match recently heard lows and highs. that way a piano solo would still get the full rainbow

two white dots

change the side we flip to if all of them are on the same side

use sin or cos or something like that for setting the speed. only flip direction if its slow enough. otherwise, decrease speed when...

change rainbow to go left to right, red to purple (or just install it upside down)

## Dad

Add a white ball to experiment with how to move a sprite against the visualizer

- streaks behind it?

have 3 layers. bottom-up in front, sprites in the middle, top-down in back

have a ball with collision. maybe as it moves through a white light it speeds up or reverses direction depending on each object's direction.

1. draw the visualizer
2. draw the sprites
3. check the overlap of the ball sprite and the visualizer and set move speed for next frame of the sprite
    - for each white light that the sprite is overlapping, increase XC with SetXChange
    - if no white lights under the sprite, decrease XC with SetXChange

or maybe the ball should move a constant direction. white leds speed up the ball. if not speed up, slow down

if we flip direction, should we reset the frames_per_shift counter to 0 or max?

have no bars. have where the white light is be the spawn point for a firework sprite

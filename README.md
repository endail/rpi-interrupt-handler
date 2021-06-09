Currently this works by setting up one thread per pin-interrupt.

It MAY be more efficient to only have one thread watching all the pin values for changes.

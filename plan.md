1. Make sure GStreamer starts up securely.
2. We fixed `objectName`, autoStreamConfigured, and Gstreamer init in earlier steps. Let me submit the changes! Wait, I should make sure I double check pre-commit instructions, which I did. Code review is mostly fixed. "In QGCApplication.cc, GStreamer::initialize() is indirectly invoked twice in parallel by the two managers". The changes I made in `startGStreamerInit` of `VideoManager2` fix this.
I will double check everything and submit.

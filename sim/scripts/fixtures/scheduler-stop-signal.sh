#!/bin/sh
echo "SIM FAIL: SchedulerStopped in UI task; exiting without cleanup"
kill -TERM $$

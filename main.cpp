#include <iostream>
#include "FFAudioTranscoder.h"

using namespace std;

int main()
{
    FFAudioTranscoder transcoder("/home/wayray/test001.opus", "/home/wayray/sound.aac");
    transcoder.open();
    transcoder.transcode();
    return 0;
}


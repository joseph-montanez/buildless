# Build-less

This is a way to do web development on an Android or iOS/iPad device.

## iOS/iPadOS with iSH

iSH is an Alpine 32-bit emulator for iOS/iPadOS. You can do all your development in here with Nano, Vim

### Updating

This command may need to be ran several times, as something may fail. I normally have to run it around 3-4 times.

    apk update && apk upgrade

Install C, Git, Vim

    apk add git gcc musl-dev vim

    git clone https://github.com/joseph-montanez/buildless.git

    cd buildness
    


### Why not NodeJS?

Running any node instance already takes around 50MB, a simple python webserver will only consume 17mb.



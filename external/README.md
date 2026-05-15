External
========

Sometimes wxPython doesn't show up on Sourceforge, so I mirrored the release in this repository.

Generating a new bottle
---------------------------
If a bottle no longer works, you can generate a new one.  Setup a system with the oldest version we're supporting.  Install the *full* version of XCode.  It should be the newest that is supported on that OS. For macOS 10.11, this is XCode 8.2.1.  You can do this headlessly, by downloading XCode from the Apple Developer website, extracting it, copying it to /Applications, accepting the license, and then building the bottle.

```
mkdir xcode
cd xcode
xip -x /vagrant/Xcode_8.2.1.xip
mv Xcode.app /Applications/
sudo xcodebuild -license accept
brew install --build-bottle FORMULA
brew bottle FORMULA
```

Then copy the generated tar.gz into this repository.

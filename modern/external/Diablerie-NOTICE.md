The DCC decoding algorithm in `../src/Dcc.cpp` and the COF layout/direction mapping
in `../src/Character.cpp` are adapted from Diablerie, Copyright (c) 2019 Alexander
Egorov, under the MIT license retained in `Diablerie-LICENSE`.

Source: https://github.com/mofr/Diablerie
Revision: `9e42ef257228257825ebbbeb905d4b1d894b6e98`
Reference files: `Assets/Scripts/Diablerie/Engine/IO/D2Formats/{DCC,COF,DirectionMapping}.cs`.

The C++ implementation adds bounded stream readers, allocation limits, and
standalone RGBA output. No Unity dependency or game artwork is vendored.

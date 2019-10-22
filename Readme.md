![](https://assets.fancy.center/assets/og_img_optimized.jpg)

## FancyCenter
Have you been dreaming of a cool car, a new gadget or some other luxury product? We are giving you a unique opportunity to buy them for a token amount of money, ranging from $1 to $20.

## What's inside
This repository contains EOS based smart contract for <https://fancy.center> website.

FancyCenter smart contract is the most secured blockchain lottery application :) Take a look at the source code and few diagrams. 

We'll explain why FancyCenter safe and fair.

## Fun Fact
Have you already used our free ticket for Apple Watch Series 3? Find more by going <https://fancy.center/item/43>

## Check Sourcecode Integrity
FancyCenter smartcontact was created using CDT v1.6.2, compilation command is ```eosio-cpp fancycenter.cpp -o fancycenter.wasm```

EOSPark source code verification doesn't support contracts created using CDTs newer than CDT v 1.5.
As mentioned here <https://eosio.github.io/eosio.cdt/latest/upgrading/1.5-to-1.6> there is no source code compatibility.

There are few simple steps to check FancyCenter smart contract source code integrity between **github** and **EOS Blockchain**:
 - checkout source code and using CDT 1.6.2 execute ```eosio-cpp fancycenter.cpp -o fancycenter.wasm```
 - execute ```shasum -a 256 fancycenter.wasm``` and save it. This is sha256 of wasm file
 - execute ```cleos -u https://api.eosnewyork.io get code fancycenter5``` and save. This is sha256 of source code retrieved from EOS blockchain
 - compare results. sha256 from blockchain should be equal to sha256 of wasm file
 
 or you can skip step 1 and checkout generated fancycenter.wasm from this repository. For current version shasum is **1ab2b6f7657a093b902c02612dbd8ead635383e0c3ee4d9516b52211ceb75fce**

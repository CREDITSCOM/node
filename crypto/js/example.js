// https://github.com/dchest/tweetnacl-js
// https://github.com/dchest/blake2s-js

let {TextEncoder, TextDecoder} = require('text-encoding');

var nacl = require('tweetnacl');
var blake2s = require('blake2s-js');

//
// Ed 25519 Key Pair generation, signature and verification
//

{   
    var testMessage = 'This is a message';
    var message = new TextEncoder().encode(testMessage);
    console.log('Message :' + message.toString());

    // Generate key pair
    var keyPair = nacl.sign.keyPair();
    
    // Valid message verification
    {
        // Sign
        var signature = nacl.sign.detached(message, keyPair.secretKey);
        console.log('Signature :' + signature);

        // Verify
        var ok = nacl.sign.detached.verify(message, signature, keyPair.publicKey);
        console.log('Valid message verification :' + ok);
    }
    
    // Invalid message verification
    {
        // Create invalid message
        var invalidMessage = 'This is test message?';
        var message = new TextEncoder().encode(invalidMessage);
        
        // Verify
        var ok = nacl.sign.detached.verify(message, signature, keyPair.publicKey);
        console.log('Invalid message verification :' + ok);
    }
    
    // Verify values taken from cscrypto library 
    {
        function hexStringToByte(str) {
          if (!str) {
            return new Uint8Array();
          }
          
          var a = [];
          for (var i = 0, len = str.length; i < len; i+=2) {
            a.push(parseInt(str.substr(i,2),16));
          }
          
          return new Uint8Array(a);
        }
        
        var publicKeyHex = '97BB18C86FAB754E3DE46A9B3330F550480EB525E3AAA19FEBC1F1AD8F6EF027';
        var privateKeyHex = '10185181807B431D077672DED634724271933313B6DF33803201E67B1B453674866548F1C590356A555A38571B484125B2CD51CD3EA50322655EECB3FBB617EA';
        var signatureHex = 'AC6B142216199F6418677ECDB4735454B9EC51BFA3E5B180F6EF90FB671BA098BD28AC9CE6E65138B56EC631E5527BB2638E3D551BF0EADEAF6C80BCC827C20C';

        var keyPair = {};
        keyPair['publicKey'] = hexStringToByte(publicKeyHex);
        keyPair['privateKey'] = hexStringToByte(privateKeyHex);
                            
        var signature = hexStringToByte(signatureHex);
        
        var hash = new blake2s(32);
        hash.update( new TextEncoder().encode(testMessage) );
             
        // Verify
        var ok = nacl.sign.detached.verify(hash.digest(), signature, keyPair.publicKey);
        console.log('Message verification :' + ok);
    }
}

//
// Blake2s hashing
//

// Empty message hash
{     
    var hash = new blake2s(32);

    console.log('Empty hash = ' + ('69217A3079908094E11121D042354A7C1F55B6482CA1A51E1B250DFD1ED0EEF9' == hash.hexDigest().toUpperCase()));   
}

// Message hash
{
    var message = 'The quick brown fox jumps over the lazy dog';
    
    var hash = new blake2s(32);
    hash.update( new TextEncoder().encode(message) );

    console.log('"' + message + '" hash = ' + ('606BEEEC743CCBEFF6CBCDF5D5302AA855C256C29B88C8ED331EA1A6BF3C8812' == hash.hexDigest().toUpperCase())); 
}
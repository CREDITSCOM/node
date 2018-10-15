
public class Example 
{
    static 
    {
        try 
        {
            System.loadLibrary("cscrypto_jni");
        } 
        catch (UnsatisfiedLinkError e) 
        {
            System.err.println("Native code library failed to load. See the chapter on Dynamic Linking Problems in the SWIG Java documentation for help.\n" + e);
            System.exit(1);
        }
    }

    public static void main(String [] args) 
    {
        // Create message
        
        String messageStr = "This is a messsage";
        int messageSize = messageStr.length();
        
        Bytes message = new Bytes(messageStr.length());
        for (int i = 0; i < messageSize; ++i)
        {
            message.setitem( i, (short)messageStr.charAt(i) );
        }
        
        // Generate Key Pair

        Bytes publicKey = new Bytes(cscrypto_constants.CSCRYPTO_PUBLIC_KEY_LENGTH.swigValue());
        Bytes privateKey = new Bytes(cscrypto_constants.CSCRYPTO_PRIVATE_KEY_LENGTH.swigValue());

        cscrypto_api.cscrypto_generate_key_pair(publicKey.cast(), privateKey.cast());

        // Hash

        Bytes hash = new Bytes(cscrypto_constants.CSCRYPTO_HASH_LENGTH.swigValue());
        cscrypto_api.cscrypto_hash(message.cast(), messageSize, hash.cast());

        // Sign

        Bytes signature = new Bytes(cscrypto_constants.CSCRYPTO_SIGNATURE_LENGTH.swigValue());
        cscrypto_api.cscrypto_sign(hash.cast(), publicKey.cast(), privateKey.cast(), signature.cast());

        // Verify

        int res = cscrypto_api.cscrypto_verify(hash.cast(), publicKey.cast(), signature.cast());
        
        System.out.println("Valid message verification " + (res == 0 ? "succeded" : "failed"));

        // Supply invalid hash
        {
            messageStr = "This is an invalid messsage";
            messageSize = messageStr.length();
            
            message = new Bytes(messageStr.length());
            for (int i = 0; i < messageSize; ++i)
            {
                message.setitem( i, (short)messageStr.charAt(i) );
            }

            // Hash

            hash = new Bytes(cscrypto_constants.CSCRYPTO_HASH_LENGTH.swigValue());
            cscrypto_api.cscrypto_hash(message.cast(), messageSize, hash.cast());

            // Verify
            
            res = cscrypto_api.cscrypto_verify(hash.cast(), publicKey.cast(), signature.cast());
            
            System.out.println("Invalid message verification " + (res == 0 ? "succeded" : "failed"));
        }
    }
}
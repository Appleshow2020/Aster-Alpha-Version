import java.math.BigInteger;

public class biginteger {
    public static void main(String[] args) {
        BigInteger a = new BigInteger("123456789012345678901234567890");
        BigInteger b = new BigInteger("9876543210");

        System.out.println(a.add(b).toString());
        System.out.println(a.subtract(b).toString());
        System.out.println(a.multiply(b).toString());
        System.out.println(a.divide(b).toString());
        System.out.println(a.remainder(b).toString());
        BigInteger[] qr = a.divideAndRemainder(b);
        System.out.println(qr[0].toString() + " r " + qr[1].toString());

        System.out.println(BigInteger.valueOf(-42).abs().toString());
        System.out.println(BigInteger.valueOf(-42).signum());
        System.out.println(BigInteger.valueOf(2).pow(100).toString());
        System.out.println(BigInteger.valueOf(48).gcd(BigInteger.valueOf(18)).toString());
        System.out.println(BigInteger.valueOf(100).mod(BigInteger.valueOf(7)).toString());

        System.out.println(new BigInteger("255").toString(16));
        System.out.println(new BigInteger("255").toString(2));
        System.out.println(new BigInteger("deadbeef", 16).toString());
        System.out.println(new BigInteger("zz", 36).toString());

        System.out.println(BigInteger.valueOf(-8).shiftRight(1).toString());
        System.out.println(BigInteger.valueOf(5).setBit(3).toString());
        System.out.println(BigInteger.valueOf(13).clearBit(2).toString());
        System.out.println(BigInteger.valueOf(5).flipBit(0).toString());
        System.out.println(BigInteger.valueOf(-1).testBit(7));

        System.out.println(BigInteger.valueOf(97).isProbablePrime(10));
        System.out.println(BigInteger.valueOf(100).isProbablePrime(10));

        System.out.println(BigInteger.ZERO.toString() + BigInteger.ONE.toString() + BigInteger.TEN.toString());
    }
}

import java.util.Arrays;
import java.util.Objects;

public class lang_wrappers {
    public static void main(String[] args) {
        System.out.println(Integer.MAX_VALUE);
        System.out.println(Integer.MIN_VALUE);
        System.out.println(Integer.parseInt("123"));
        System.out.println(Integer.parseInt("-456"));
        System.out.println(Integer.parseInt("ff", 16));
        System.out.println(Integer.parseInt("1010", 2));
        System.out.println(Integer.toString(255, 16));
        System.out.println(Integer.toString(-255, 16));
        System.out.println(Integer.toString(Integer.MIN_VALUE, 2));
        System.out.println(Integer.toHexString(-1));
        System.out.println(Integer.toOctalString(8));
        System.out.println(Integer.toBinaryString(10));
        System.out.println(Integer.compare(3, 9));
        System.out.println(Integer.compareUnsigned(-1, 1));
        System.out.println(Integer.signum(-5));
        System.out.println(Integer.max(3, 9));
        System.out.println(Integer.bitCount(-1));
        System.out.println(Integer.highestOneBit(100));
        System.out.println(Integer.lowestOneBit(12));
        System.out.println(Integer.highestOneBit(-1));
        System.out.println(Integer.highestOneBit(Integer.MIN_VALUE));
        System.out.println(Integer.lowestOneBit(Integer.MIN_VALUE));

        System.out.println(Long.parseLong("9223372036854775807"));
        System.out.println(Long.toString(-255L, 16));
        System.out.println(Long.toString(Long.MIN_VALUE, 16));
        System.out.println(Long.parseLong("ff", 16));
        System.out.println(Long.toHexString(-1));
        System.out.println(Long.toHexString(Long.MIN_VALUE));
        System.out.println(Long.toBinaryString(10));
        System.out.println(Long.compareUnsigned(-1, 1));
        System.out.println(Long.bitCount(-1));

        System.out.println(Math.abs(-7));
        System.out.println(Math.abs(-2.5));
        System.out.println(Math.max(3, 9));
        System.out.println(Math.min(2.5, 1.5));
        System.out.println(Math.floorDiv(7, 2));
        System.out.println(Math.floorDiv(-7, 2));
        System.out.println(Math.floorMod(-7, 2));
        System.out.println(Math.sqrt(144.0));
        System.out.println(Math.pow(2.0, 10.0));
        System.out.println(Math.round(2.5));
        System.out.println(Math.round(-2.5));

        System.out.println(Arrays.toString(new int[]{3, 1, 2}));
        System.out.println(Arrays.toString(new String[]{"a", "b"}));
        System.out.println(Arrays.deepToString(new int[][]{{1, 2}, {3}}));
        System.out.println(Objects.equals(null, null));
        System.out.println(Objects.equals("a", "a"));
        System.out.println(Objects.isNull(null));
        System.out.println(Objects.toString(null));
    }
}

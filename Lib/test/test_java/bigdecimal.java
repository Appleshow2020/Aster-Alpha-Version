import java.math.BigDecimal;
import java.math.RoundingMode;

public class bigdecimal {
    public static void main(String[] args) {
        BigDecimal x = new BigDecimal("123.45");
        BigDecimal y = new BigDecimal("-0.0067");

        System.out.println(x.add(y).toString());
        System.out.println(x.subtract(y).toString());
        System.out.println(x.multiply(y).toString());
        System.out.println(new BigDecimal("1").divide(new BigDecimal("8")).toString());
        System.out.println(new BigDecimal("10.00").divide(new BigDecimal("4")).toString());
        System.out.println(new BigDecimal("1").divide(new BigDecimal("3"), 5, RoundingMode.HALF_UP).toString());

        RoundingMode[] modes = {
            RoundingMode.UP, RoundingMode.DOWN, RoundingMode.CEILING, RoundingMode.FLOOR,
            RoundingMode.HALF_UP, RoundingMode.HALF_DOWN, RoundingMode.HALF_EVEN,
        };
        for (RoundingMode mode : modes) {
            System.out.println(mode.name() + " "
                + new BigDecimal("1").divide(new BigDecimal("3"), 3, mode).toString());
            System.out.println(mode.name() + " "
                + new BigDecimal("-2.5").setScale(0, mode).toString());
        }

        System.out.println(new BigDecimal("1.5").pow(3).toString());
        System.out.println(new BigDecimal("1.2300").stripTrailingZeros().toString());
        System.out.println(new BigDecimal("123.45").movePointLeft(2).toString());
        System.out.println(new BigDecimal("0.000123").movePointRight(3).toString());
        System.out.println(new BigDecimal("12.340").compareTo(new BigDecimal("12.34")));
        System.out.println(new BigDecimal("12.340").equals(new BigDecimal("12.34")));
        System.out.println(new BigDecimal("12.9").intValue());
        System.out.println(new BigDecimal("123.4500").scale());
        System.out.println(new BigDecimal("123.4500").precision());
        System.out.println(new BigDecimal("123.4500").unscaledValue().toString());
    }
}

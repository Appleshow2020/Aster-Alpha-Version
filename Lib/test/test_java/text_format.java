import java.text.DecimalFormat;
import java.text.MessageFormat;
import java.util.Locale;

public class text_format {
    public static void main(String[] args) throws Exception {
        DecimalFormat df = new DecimalFormat("#,##0.00");
        System.out.println(df.toPattern());
        System.out.println(df.format(1234567.891));
        System.out.println(df.format(-1234.5));
        System.out.println(df.format(0));

        DecimalFormat df2 = new DecimalFormat("0.###");
        System.out.println(df2.format(3.14159));
        System.out.println(df2.format(2.0));

        DecimalFormat df3 = new DecimalFormat("0.00%");
        System.out.println(df3.format(0.25));
        System.out.println(df3.parse("25.00%"));

        DecimalFormat df4 = new DecimalFormat("$#,##0.00");
        System.out.println(df4.format(1234.5));
        System.out.println(df4.format(-1234.5));
        System.out.println(df4.parse("-$1,234.50"));

        MessageFormat mf = new MessageFormat("Hello, {0}! You have {1} messages.", Locale.US);
        System.out.println(mf.format(new Object[]{"Aster", 3}));
        System.out.println(MessageFormat.format("It''s {0} at {1}", "Aster", 3));
    }
}

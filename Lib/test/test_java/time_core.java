import java.time.Duration;
import java.time.Instant;
import java.time.LocalDate;
import java.time.LocalTime;
import java.time.Period;

public class time_core {
    public static void main(String[] args) {
        LocalDate d = LocalDate.of(2024, 2, 29);
        System.out.println(d.toString());
        System.out.println(d.plusDays(1).toString());
        System.out.println(d.plusDays(365).toString());
        System.out.println(d.plusMonths(1).toString());
        System.out.println(LocalDate.of(2024, 1, 31).plusMonths(1).toString());
        System.out.println(d.minusDays(1).toString());
        System.out.println(d.toEpochDay());
        System.out.println(LocalDate.ofEpochDay(19782).toString());
        System.out.println(d.getDayOfYear());
        System.out.println(d.getDayOfWeek().name());

        LocalTime t = LocalTime.of(23, 59, 59, 123000000);
        System.out.println(t.toString());
        System.out.println(LocalTime.of(21, 13).toString());
        System.out.println(LocalTime.of(21, 13, 0, 1).toString());
        System.out.println(LocalTime.of(1, 2, 3).toString());
        System.out.println(t.plusNanos(1000000).toString());
        System.out.println(t.plusSeconds(1).toString());
        System.out.println(t.toSecondOfDay());
        System.out.println(t.toNanoOfDay());
        System.out.println(LocalTime.parse("07:08:09.010").toString());

        Instant i = Instant.parse("2024-02-29T12:30:45.123Z");
        System.out.println(i.toString());
        System.out.println(Instant.parse("1998-07-04T08:01:00Z").toString());
        System.out.println(Instant.ofEpochSecond(1, 0).toString());
        System.out.println(Instant.ofEpochSecond(1, 123456789).toString());
        System.out.println(i.getEpochSecond());
        System.out.println(i.getNano());
        System.out.println(i.toEpochMilli());
        System.out.println(i.plusSeconds(90).toString());
        System.out.println(i.minusMillis(500).toString());

        Duration du = Duration.parse("PT1H30M");
        System.out.println(du.toString());
        System.out.println(du.getSeconds());
        System.out.println(du.getNano());
        System.out.println(Duration.ofMillis(1500).toString());
        System.out.println(Duration.ofNanos(1234567890).toString());
        System.out.println(Duration.parse("-PT0.5S").getSeconds());
        System.out.println(Duration.parse("-PT0.5S").getNano());

        Period p = Period.parse("P1Y2M3D");
        System.out.println(p.toString());
        System.out.println(p.toTotalMonths());
        System.out.println(p.plusMonths(10).toString());
    }
}

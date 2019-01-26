package nl.persoonlijk.hva;

import javax.servlet.ServletException;
import javax.servlet.annotation.WebServlet;
import javax.servlet.http.HttpServlet;
import javax.servlet.http.HttpServletRequest;
import javax.servlet.http.HttpServletResponse;
import java.io.IOException;
import com.pi4j.io.gpio.*;

import java.io.PrintWriter;

@WebServlet(name = "/Servlet",  urlPatterns={ "/test.html" })


public class Servlet extends HttpServlet {

    public static void main(String[] args) throws InterruptedException {


        System.out.println("<--Pi4J--> GPIO Control Example ... started.");

        // create gpio controller
        final GpioController gpio = GpioFactory.getInstance();

        // provision gpio pin #01 as an output pin and turn on
        final GpioPinDigitalOutput pin = gpio.provisionDigitalOutputPin(RaspiPin.GPIO_01, "MyLED", PinState.HIGH);

        // set shutdown state for this pin
        pin.setShutdownOptions(true, PinState.LOW);


        // turn on gpio pin #01 for 1 second and then off
        System.out.println("--> GPIO state should be: ON for only 1 second");
        pin.pulse(1000, true); // set second argument to 'true' use a blocking call

        // stop all GPIO activity/threads by shutting down the GPIO controller
        // (this method will forcefully shutdown all GPIO monitoring threads and scheduled tasks)
        gpio.shutdown();

        System.out.println("Exiting ControlGpioExample");
    }
    protected void doPost(HttpServletRequest request, HttpServletResponse response) throws ServletException, IOException {


        // Inloggen main pagina params
        String username = request.getParameter("voornaam");
        String password = request.getParameter("achternaam");

        //voor mij
        System.out.println(username + " " +  password);


        //check credentials main acces
        if (username.equals("DRVHP12") &&  password.equals("kaas")) {
            response.sendRedirect("home.html");
        } else {
            response.sendRedirect("index.html");

        }
        String button = request.getParameter("upvote");

        if ("upvote".equals(button)) {
            System.out.println("werkt dit");
            //doe stuff
        }


    }

    protected void doGet(HttpServletRequest request, HttpServletResponse response) throws ServletException, IOException {
        //empty
    }
}

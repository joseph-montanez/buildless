function About() {
    return React.createElement(
        "div",
        null,
        React.createElement(Header, null),
        React.createElement("h1", null, "About Us"),
        React.createElement("p", null, "This is the about page content."),
        React.createElement(Footer, null)
    );
}

window.About = About;

import { LitElement, html } from 'lit';

export class HomeComponent extends LitElement {
    render() {
        return html`
                    <h1>Welcome to the Home Page!!!</h1>
                    <p>This is the home page content.</p>
                    <img src="images/pexels-david-gari-20421927.jpg" alt="" height="300">
                `;
    }
}
customElements.define('home-component', HomeComponent);